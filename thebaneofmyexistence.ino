#include <DS18B20.h>
#include <Adafruit_DHT.h>

#define DHTPIN D2
#define DHTTYPE DHT11

#define wlpin A5 // water level
#define wtpin D8 // water temp
#define pHpin A0

double humidity;
double waterLevel;
double waterTemp;
double pHVal;

unsigned long int avgVal;
int buffer_arr[10], temp;
float calibration_value = 21.34; // adjust as needed

String lastEvent; // records the last time a message was sent out (hourly basis)
int lastEmgMsg;
int firstCallHour;

boolean unassignedLast = false;
boolean firstCall = false; // call hourlyUpdate on startup

DHT dht(DHTPIN, DHTTYPE); // humidity sensor
DS18B20 water_temp(wtpin, true); 

String getHour() {
    Time.beginDST();
    Time.zone(-6); // time zone offset for CST
    int hour = int(Time.hour());
    int minute = int(Time.minute());
    String str_hr = String(hour);
    String str_min = String(minute);
    if(str_hr.length() < 2) { // checks if hour is zero-padded
        str_hr = "0" + str_hr;
    }
    if(str_min.length() < 2) { // checks if min is zero-padded
        str_min = "0" + str_min;
    }
    return str_hr + ":" + str_min; // HH:MM format
}

boolean getHumidity() {
    float temp_humidity = dht.getHumidity();
    if(isnan(temp_humidity)) {
       getHumidity();
    }
    else {
        humidity = temp_humidity;
    }
    Serial.println();
    Serial.print("Humidity level: ");
    Serial.println(String(humidity));
    delay(3000);
    return humidity >= 50 && humidity <= 70; // ideal conditions for hydroponic plants is between 50-70 rh
}

boolean getWaterLevel() {
    int tempLevel = 0;
    for(int i = 0; i < 5; i++) {
        delay(1000);
        tempLevel += analogRead(wlpin);
    }
    waterLevel = (tempLevel / 5); // get the avg water level for 5 samples
    Serial.print("Water Level: ");
	Serial.println(waterLevel);
	delay(5000);
	return waterLevel < 830; // above 830 indicates flooding
}

boolean getWaterTemp() {
    float temp_temp = water_temp.getTemperature();
    if(!water_temp.crcCheck()) {
        getWaterTemp();
    }
    else {
        waterTemp = water_temp.convertToFahrenheit(temp_temp);
        Serial.print("Water Temperature: ");
        Serial.println(waterTemp);
    }
    delay(5000);
    return waterTemp >= 65 && waterTemp <= 80; // ideal conditions are 65-80 deg Fah
}

boolean getPH() {
    // get 10 readings to smooth out the avg volt
    for(int i = 0; i < 10; i++) {
        buffer_arr[i] = analogRead(pHpin);
        delay(30);
    }
    // sort the stored values in ascending order
    for(int i = 0; i < 9; i++) {
        for(int x = i + 1; x < 10; x++) {
            if(buffer_arr[i] > buffer_arr[x]) {
                temp = buffer_arr[i];
                buffer_arr[i] = buffer_arr[x];
                buffer_arr[x] = temp;
            }
        }
    }
    avgVal = 0;
    // calculate the average pH from the center 6 voltages
    for(int i = 2; i < 8; i++) {
        avgVal += buffer_arr[i];
        float volt = analogRead(pHpin) * 3.3 / 4095 / 6;
        pHVal = -5.70 * volt + calibration_value;
    }
    return pHVal >= 5.5 && pHVal <= 6.2;
}

void sendHourlyUpdate() {
    String rh = "Here is Your Hourly Update:\r- The relative humidity is roughly: ";
    String wl = "%.\r- The water level is stable at: ";
    String wt = ".\r- The water temperature is at: ";
    String pH = "\xB0""F\r- And the pH of your solution is at: ";
    String end = " :)";
    String message = rh + String(humidity).substring(0,4) + wl + String(waterLevel).substring(0,3) + wt + String(waterTemp).substring(0,4) + pH + String(pHVal).substring(0,3) + end;
    Particle.publish("twilio_sms", message, PRIVATE);
    lastEvent = getHour();
    Serial.println();
    Serial.print("Next Hourly Update @ ");
    Serial.println((lastEvent.substring(0, 2).toInt() + 1) % 24);
    delay(5000);
}

void sendEmergencyMessage() { // assumes that a value is unstable based on loop()
    Time.beginDST();
    Time.zone(-6); // time zone offset for CST
    int current_hour = Time.hour();
    Serial.println("current hr: ");
    Serial.println(current_hour);
    if(unassignedLast == false || (current_hour >= ((lastEmgMsg + 1) % 24)) || current_hour >= (firstCallHour + 1)) { // makes sure msgs are sent sparingly
        String msg = "Alert!";
        if(getHumidity() == false) {
            msg += " Humidity is at ";
            msg += String(humidity).substring(0,4);
            msg += "%!";
        }
        if(getWaterLevel() == false) {
            msg += " Water level is at ";
            msg += String(waterLevel).substring(0,3);
            msg += "!";
        }
        if(getWaterTemp() == false) {
            msg += " Water temperature is at ";
            msg += String(waterTemp).substring(0,4);
            msg += "\xB0""F!";
        }
        if(getPH() == false) {
            msg += " PH is at ";
            msg += String(pHVal).substring(0,3);
            msg += "!";
        }
        Particle.publish("emergency_msg", msg, PRIVATE);
        unassignedLast = true; // confirms that an emergency msg has been sent and a time has been assigned
        lastEmgMsg = current_hour;
    }
    delay(5000);
}

void setup() {
    Serial.begin(9600);
    dht.begin();
}

void loop() {
    Time.beginDST();
    Time.zone(-6); // time zone offset for CST
    // first hourly update {
    if(firstCall == false) {
        getHumidity();
        getWaterLevel();
        getWaterTemp();
        getPH();
        sendHourlyUpdate();
        firstCall = true;
        firstCallHour = Time.hour();
    }
    // checks when it's time to send another msg
    int current_hour = (int)Time.hour();
    int current_minute = (int)Time.minute();
    if(current_hour == (lastEvent.substring(0, 2).toInt() + 1) % 24) {
        if(current_minute == lastEvent.substring(3, 5).toInt()) {
            sendHourlyUpdate();
        }
    }
    // check if all sensors are stable
    if(getHumidity() == false || getWaterLevel() == false || getWaterTemp() == false || getPH() == false) {
        sendEmergencyMessage();
    }
    delay(30000); // delay 20 seconds
}
