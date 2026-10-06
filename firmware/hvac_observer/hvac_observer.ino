/* UNO Q / Arduino Zephyr core. Passive sensing only: no actuator outputs.
 * SHT31 x2, SDP810, independent float and optional isolated control inputs.
 * Refer to hardware/README.md before enabling input circuitry.
 */
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_RouterBridge.h>
#include <math.h>

// Set true ONLY after isolated AC input channels have been commissioned.
constexpr bool CONTROL_INPUTS_INSTALLED = false;
constexpr bool FLOAT_INSTALLED = false;
constexpr uint8_t controlPins[] = {2,3,4,5,6,7,8}; // R,Y,Y2,W,W2,G,OB
const char* controlNames[] = {"R","Y","Y2","W","W2","G","OB"};
uint32_t lastLow[7] = {};
bool seenLow[7] = {};
uint32_t seq=0, lastAcquire=0;
String snapshot="{\"seq\":0,\"stage\":1}";

uint8_t crc8(const uint8_t* p, uint8_t n) {
  uint8_t c=0xff;
  while(n--) { c ^= *p++; for(uint8_t i=0;i<8;i++) c=(c&0x80)?(c<<1)^0x31:c<<1; }
  return c;
}
bool readSHT(uint8_t address,float &t,float &h) {
  t=h=NAN;
  Wire.beginTransmission(address); Wire.write(0x24); Wire.write(0x00);
  if(Wire.endTransmission()!=0) return false;
  delay(20);
  if(Wire.requestFrom(address,(uint8_t)6)!=6) return false;
  uint8_t b[6]; for(auto &v:b) v=Wire.read();
  if(crc8(b,2)!=b[2] || crc8(b+3,2)!=b[5]) return false;
  t=-45+175.0*((uint16_t(b[0])<<8)|b[1])/65535.0;
  h=100.0*((uint16_t(b[3])<<8)|b[4])/65535.0;
  return true;
}
float readPressure() {
  // SDP8xx triggered differential-pressure, no clock stretching: 0x362F.
  Wire.beginTransmission(0x25); Wire.write(0x36); Wire.write(0x2f);
  if(Wire.endTransmission()!=0) return NAN;
  delay(50);
  if(Wire.requestFrom((uint8_t)0x25,(uint8_t)9)!=9) return NAN;
  uint8_t b[9]; for(auto &v:b) v=Wire.read();
  if(crc8(b,2)!=b[2] || crc8(b+3,2)!=b[5] || crc8(b+6,2)!=b[8]) return NAN;
  int16_t raw=(uint16_t(b[0])<<8)|b[1];
  uint16_t scale=(uint16_t(b[6])<<8)|b[7];
  return scale ? float(raw)/scale : NAN;
}
String number(float v) { return isfinite(v) ? String(v,3) : String("null"); }
String getSnapshot() { return snapshot; }
void setup() {
  Wire.begin(); Wire.setClock(100000);
  for(auto p:controlPins) pinMode(p,INPUT_PULLUP);
  pinMode(9,INPUT_PULLUP);
  Bridge.begin();
  Bridge.provide_safe("hvac_snapshot",getSnapshot);
}
void loop() {
  uint32_t now=millis();
  // AC optocouplers pulse each half-cycle. Latch pulses for 150 ms.
  for(uint8_t i=0;i<7;i++) if(digitalRead(controlPins[i])==LOW) { lastLow[i]=now; seenLow[i]=true; }
  if(now-lastAcquire>=2000) {
    lastAcquire=now;
    float rt,rh,st,sh;
    readSHT(0x44,rt,rh); readSHT(0x45,st,sh);
    float dp=readPressure();
    bool secondStage=CONTROL_INPUTS_INSTALLED && ((seenLow[2] && now-lastLow[2]<150) || (seenLow[4] && now-lastLow[4]<150));
    String s="{\"seq\":"+String(++seq)+",\"stage\":"+String(secondStage?2:1)+",\"return_c\":"+number(rt)+",\"supply_c\":"+number(st);
    s+=",\"indoor_rh\":"+number(rh)+",\"supply_rh\":"+number(sh)+",\"filter_pa\":"+number(dp);
    for(uint8_t i=0;i<7;i++) {
      s+=",\""+String(controlNames[i])+"\":";
      s+=CONTROL_INPUTS_INSTALLED ? (seenLow[i] && now-lastLow[i]<150 ? "true":"false") : "null";
    }
    s+=",\"condensate\":";
    s+=FLOAT_INSTALLED ? (digitalRead(9)==LOW?"true":"false") : "null";
    s+="}";
    snapshot=s;
  }
  delay(1);
}
