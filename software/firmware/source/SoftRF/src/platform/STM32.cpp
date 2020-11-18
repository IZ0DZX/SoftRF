/*
 * Platform_STM32.cpp
 * Copyright (C) 2019-2020 Linar Yusupov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(ARDUINO_ARCH_STM32)

#include <SPI.h>
#include <Wire.h>
#include <IWatchdog.h>

#include "../system/SoC.h"
#include "../driver/RF.h"
#include "../driver/LED.h"
#include "../driver/Sound.h"
#include "../driver/EEPROM.h"
#include "../driver/Battery.h"
#include "../driver/OLED.h"
#include "../protocol/data/NMEA.h"
#include "../protocol/data/GDL90.h"
#include "../protocol/data/D1090.h"

#include <STM32LowPower.h>

// RFM95W pin mapping
lmic_pinmap lmic_pins = {
    .nss = SOC_GPIO_PIN_SS,
    .txe = LMIC_UNUSED_PIN,
    .rxe = LMIC_UNUSED_PIN,
#if !defined(USE_OGN_RF_DRIVER)
    .rst = LMIC_UNUSED_PIN,
    .dio = {LMIC_UNUSED_PIN, LMIC_UNUSED_PIN, LMIC_UNUSED_PIN},
#else
    .rst = SOC_GPIO_PIN_RST,
    .dio = {SOC_GPIO_PIN_DIO0, LMIC_UNUSED_PIN, LMIC_UNUSED_PIN},
#endif
    .busy = LMIC_UNUSED_PIN,
    .tcxo = LMIC_UNUSED_PIN,
};

#if defined(USBD_USE_CDC) && !defined(DISABLE_GENERIC_SERIALUSB)
HardwareSerial Serial1(SOC_GPIO_PIN_CONS_RX,  SOC_GPIO_PIN_CONS_TX);
#endif

#if defined(ARDUINO_NUCLEO_L073RZ)

HardwareSerial Serial2(USART2);
HardwareSerial Serial4(SOC_GPIO_PIN_SWSER_RX, SOC_GPIO_PIN_SWSER_TX);

#elif defined(ARDUINO_BLUEPILL_F103CB)

HardwareSerial Serial2(SOC_GPIO_PIN_SWSER_RX, SOC_GPIO_PIN_SWSER_TX);
HardwareSerial Serial3(SOC_GPIO_PIN_RX3,      SOC_GPIO_PIN_TX3);

#else
#error "This hardware platform is not supported!"
#endif

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIX_NUM, SOC_GPIO_PIN_LED,
                              NEO_GRB + NEO_KHZ800);

static int stm32_board = STM32_BLUE_PILL; /* default */

char UDPpacketBuffer[4]; // Dummy definition to satisfy build sequence

static struct rst_info reset_info = {
  .reason = REASON_DEFAULT_RST,
};

static uint32_t bootCount = 0;

static int STM32_probe_pin(uint32_t pin, uint32_t mode)
{
  int rval;

  pinMode(pin, mode);
  delay(20);
  rval = digitalRead(pin);
  pinMode(pin, INPUT);

  return rval;
}

static void STM32_SerialWakeup() { }

static void STM32_setup()
{
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST))
    {
        reset_info.reason = REASON_WDT_RST; // "LOW_POWER_RESET"
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST))
    {
        reset_info.reason = REASON_WDT_RST; // "WINDOW_WATCHDOG_RESET"
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
    {
        reset_info.reason = REASON_SOFT_WDT_RST; // "INDEPENDENT_WATCHDOG_RESET"
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))
    {
        // This reset is induced by calling the ARM CMSIS `NVIC_SystemReset()` function!
        reset_info.reason = REASON_SOFT_RESTART; // "SOFTWARE_RESET"
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))
    {
        reset_info.reason = REASON_DEFAULT_RST; // "POWER-ON_RESET (POR) / POWER-DOWN_RESET (PDR)"
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))
    {
        reset_info.reason = REASON_EXT_SYS_RST; // "EXTERNAL_RESET_PIN_RESET"
    }

    // Clear all the reset flags or else they will remain set during future resets until system power is fully removed.
    __HAL_RCC_CLEAR_RESET_FLAGS();

    LowPower.begin();

    hw_info.model = SOFTRF_MODEL_RETRO;

#if defined(ARDUINO_NUCLEO_L073RZ)
    stm32_board = STM32_TTGO_TWATCH_EB_1_3;

    /* Probe on presence of external pull-up resistors connected to I2C bus */
    if (            STM32_probe_pin(SOC_GPIO_PIN_SCL, INPUT_PULLDOWN) == HIGH  &&
        (delay(50), STM32_probe_pin(SOC_GPIO_PIN_SCL, INPUT_PULLDOWN) == HIGH) &&
                    STM32_probe_pin(SOC_GPIO_PIN_SDA, INPUT_PULLDOWN) == HIGH  &&
        (delay(50), STM32_probe_pin(SOC_GPIO_PIN_SDA, INPUT_PULLDOWN) == HIGH)) {

      hw_info.model = SOFTRF_MODEL_DONGLE;
      stm32_board   = STM32_TTGO_TMOTION_1_1;
    }
#elif defined(ARDUINO_BLUEPILL_F103CB)
    stm32_board = STM32_BLUE_PILL;
#else
#error "This hardware platform is not supported!"
#endif

#if defined(USBD_USE_CDC) && !defined(DISABLE_GENERIC_SERIALUSB)
    SerialOutput.begin(SERIAL_OUT_BR, SERIAL_OUT_BITS);
#endif

    uint32_t boot_action = getBackupRegister(BOOT_ACTION_INDEX);

    switch (boot_action)
    {
#if defined(USE_SERIAL_DEEP_SLEEP)
    case STM32_BOOT_SERIAL_DEEP_SLEEP:
#if !defined(USBD_USE_CDC) || defined(DISABLE_GENERIC_SERIALUSB)
      SerialOutput.begin(SERIAL_OUT_BR, SERIAL_OUT_BITS);
#endif
      LowPower.enableWakeupFrom(&SerialOutput, STM32_SerialWakeup);

      LowPower.deepSleep();

      /* reset onto default value */
      setBackupRegister(BOOT_ACTION_INDEX, STM32_BOOT_NORMAL);

      // Empty Serial Rx
      while(SerialOutput.available()) {
        char c = SerialOutput.read();
      }
      break;
#endif
    case STM32_BOOT_SHUTDOWN:
      LowPower_shutdown();
      break;
    case STM32_BOOT_NORMAL:
    default:
      break;
    }

    bootCount = getBackupRegister(BOOT_COUNT_INDEX);
    bootCount++;
    setBackupRegister(BOOT_COUNT_INDEX, bootCount);

    pinMode(SOC_GPIO_PIN_BATTERY, INPUT_ANALOG);

    Wire.setSCL(SOC_GPIO_PIN_SCL);
    Wire.setSDA(SOC_GPIO_PIN_SDA);
}

static void STM32_post_init()
{
#if defined(USE_OLED)
  OLED_info1();
#endif /* USE_OLED */
}

static void STM32_loop()
{
  // Reload the watchdog
  if (IWatchdog.isEnabled()) {
    IWatchdog.reload();
  }
}

static void STM32_fini()
{
#if defined(ARDUINO_NUCLEO_L073RZ)

#if 0
  /* Idle */
  swSer.write("@GSTP\r\n"); delay(250);

  /* GNSS sleep level 0-2 */
//  swSer.write("@SLP 0\r\n");
  swSer.write("@SLP 1\r\n");
//  swSer.write("@SLP 2\r\n");

  swSer.flush(); delay(100);
#endif

  /* De-activate 1.8V<->3.3V level shifters */
  digitalWrite(SOC_GPIO_PIN_GNSS_LS, LOW);
  delay(100);
  pinMode(SOC_GPIO_PIN_GNSS_LS, INPUT);

#endif /* ARDUINO_NUCLEO_L073RZ */

  swSer.end();
  SPI.end();
  Wire.end();

  /*
   * Work around an issue that
   * WDT (once enabled) is active all the time
   * until hardware restart
   */
#if defined(USE_SERIAL_DEEP_SLEEP)
  setBackupRegister(BOOT_ACTION_INDEX, STM32_BOOT_SERIAL_DEEP_SLEEP);
#else
  setBackupRegister(BOOT_ACTION_INDEX, STM32_BOOT_SHUTDOWN);
#endif

  HAL_NVIC_SystemReset();
}

static void STM32_reset()
{
  HAL_NVIC_SystemReset();
}

static uint32_t STM32_getChipId()
{
#if !defined(SOFTRF_ADDRESS)
  /* Same method as STM32 OGN tracker does */
  uint32_t id = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();

  /* remap address to avoid overlapping with congested FLARM range */
  if (((id & 0x00FFFFFF) >= 0xDD0000) && ((id & 0x00FFFFFF) <= 0xDFFFFF)) {
    id += 0x100000;
  }

  return id;
#else
  return (SOFTRF_ADDRESS & 0xFFFFFFFFU );
#endif
}

static void* STM32_getResetInfoPtr()
{
  return (void *) &reset_info;
}

static String STM32_getResetInfo()
{
  switch (reset_info.reason)
  {
    default                     : return F("No reset information available");
  }
}

static String STM32_getResetReason()
{
  switch (reset_info.reason)
  {
    case REASON_DEFAULT_RST       : return F("DEFAULT");
    case REASON_WDT_RST           : return F("WDT");
    case REASON_EXCEPTION_RST     : return F("EXCEPTION");
    case REASON_SOFT_WDT_RST      : return F("SOFT_WDT");
    case REASON_SOFT_RESTART      : return F("SOFT_RESTART");
    case REASON_DEEP_SLEEP_AWAKE  : return F("DEEP_SLEEP_AWAKE");
    case REASON_EXT_SYS_RST       : return F("EXT_SYS");
    default                       : return F("NO_MEAN");
  }
}

#include <malloc.h>
extern "C" char *sbrk(int);
/* Use linker definition */
extern char _estack;
extern char _Min_Stack_Size;

static char *minSP = (char*)(&_estack - &_Min_Stack_Size);

static uint32_t STM32_getFreeHeap()
{
  char *heapend = (char*)sbrk(0);
  char * stack_ptr = (char*)__get_MSP();
  struct mallinfo mi = mallinfo();

  return ((stack_ptr < minSP) ? stack_ptr : minSP) - heapend + mi.fordblks ;
}

static long STM32_random(long howsmall, long howBig)
{
  return random(howsmall, howBig);
}

static void STM32_Sound_test(int var)
{
  if (settings->volume != BUZZER_OFF) {
    tone(SOC_GPIO_PIN_BUZZER, 440,  500); delay(500);
    tone(SOC_GPIO_PIN_BUZZER, 640,  500); delay(500);
    tone(SOC_GPIO_PIN_BUZZER, 840,  500); delay(500);
    tone(SOC_GPIO_PIN_BUZZER, 1040, 500); delay(600);
  }
}

static void STM32_WiFi_set_param(int ndx, int value)
{
  /* NONE */
}

static void STM32_WiFi_transmit_UDP(int port, byte *buf, size_t size)
{
  /* NONE */
}

static bool STM32_EEPROM_begin(size_t size)
{
  if (size > E2END) {
    return false;
  }

  EEPROM.begin();

  if (settings->nmea_out == NMEA_BLUETOOTH) {
#if defined(USBD_USE_CDC) && !defined(DISABLE_GENERIC_SERIALUSB)
    settings->nmea_out = NMEA_USB;
#else
    settings->nmea_out = NMEA_UART;
#endif
  }
  if (settings->gdl90 == GDL90_BLUETOOTH) {
#if defined(USBD_USE_CDC) && !defined(DISABLE_GENERIC_SERIALUSB)
    settings->gdl90 = GDL90_USB;
#else
    settings->gdl90 = GDL90_UART;
#endif
  }
  if (settings->d1090 == D1090_BLUETOOTH) {
#if defined(USBD_USE_CDC) && !defined(DISABLE_GENERIC_SERIALUSB)
    settings->d1090 = D1090_USB;
#else
    settings->d1090 = D1090_UART;
#endif
  }

  return true;
}

static void STM32_SPI_begin()
{
  SPI.setMISO(SOC_GPIO_PIN_MISO);
  SPI.setMOSI(SOC_GPIO_PIN_MOSI);
  SPI.setSCLK(SOC_GPIO_PIN_SCK);
  // Slave Select pin is driven by RF driver

  SPI.begin();
}

static void STM32_swSer_begin(unsigned long baud)
{

  swSer.begin(baud);

#if defined(ARDUINO_NUCLEO_L073RZ)
  /* drive GNSS RST pin low */
  pinMode(SOC_GPIO_PIN_GNSS_RST, OUTPUT);
  digitalWrite(SOC_GPIO_PIN_GNSS_RST, LOW);

  /* activate 1.8V<->3.3V level shifters */
  pinMode(SOC_GPIO_PIN_GNSS_LS,  OUTPUT);
  digitalWrite(SOC_GPIO_PIN_GNSS_LS,  HIGH);

  /* keep RST low to ensure proper IC reset */
  delay(200);

  /* release */
  digitalWrite(SOC_GPIO_PIN_GNSS_RST, HIGH);

  /* give Sony GNSS few ms to warm up */
  delay(100);

  /* Leave pin floating */
  pinMode(SOC_GPIO_PIN_GNSS_RST, INPUT);

#if 0
  // swSer.write("@VER\r\n");

  /* Idle */
  // swSer.write("@GSTP\r\n");      delay(250);

  /* GGA + GSA + RMC */
  swSer.write("@BSSL 0x25\r\n"); delay(250);
  /* GPS + GLONASS */
  swSer.write("@GNS 0x3\r\n");   delay(250);
#if SOC_GPIO_PIN_GNSS_PPS != SOC_UNUSED_PIN
  /* Enable 1PPS output */
  swSer.write("@GPPS 0x1\r\n");  delay(250);
#endif

  // swSer.write("@GSW\r\n"); /* warm start */

  rst_info *resetInfo = (rst_info *) SoC->getResetInfoPtr();

  if (resetInfo->reason == REASON_DEFAULT_RST) {
    swSer.write("@GCD\r\n"); /* cold start */
  } else {
    swSer.write("@GSR\r\n"); /* hot  start */
  }

  delay(250);
#endif
#endif /* ARDUINO_NUCLEO_L073RZ */
}

static void STM32_swSer_enableRx(boolean arg)
{
  /* NONE */
}

static byte STM32_Display_setup()
{
  byte rval = DISPLAY_NONE;

#if defined(USE_OLED)
  rval = OLED_setup();
#endif /* USE_OLED */

  return rval;
}

static void STM32_Display_loop()
{
#if defined(USE_OLED)
  OLED_loop();
#endif /* USE_OLED */
}

static void STM32_Display_fini(const char *msg)
{
#if defined(USE_OLED)
  OLED_fini(msg);
#endif /* USE_OLED */
}

static void STM32_Battery_setup()
{

}

static float STM32_Battery_voltage()
{
#ifdef __LL_ADC_CALC_VREFANALOG_VOLTAGE
  int32_t Vref = (__LL_ADC_CALC_VREFANALOG_VOLTAGE(analogRead(AVREF), LL_ADC_RESOLUTION));
#else
  int32_t Vref = (VREFINT * ADC_RANGE / analogRead(AVREF)); // ADC sample to mV
#endif

  int32_t mV = (__LL_ADC_CALC_DATA_TO_VOLTAGE(Vref,
                                              analogRead(SOC_GPIO_PIN_BATTERY),
                                              LL_ADC_RESOLUTION));

  return mV * SOC_ADC_VOLTAGE_DIV / 1000.0;
}

void STM32_GNSS_PPS_Interrupt_handler() {
  PPS_TimeMarker = millis();
}

static unsigned long STM32_get_PPS_TimeMarker() {
  return PPS_TimeMarker;
}

static bool STM32_Baro_setup() {
  return true;
}

static void STM32_UATSerial_begin(unsigned long baud)
{
  UATSerial.begin(baud);
}

static void STM32_UATModule_restart()
{
  digitalWrite(SOC_GPIO_PIN_TXE, LOW);
  pinMode(SOC_GPIO_PIN_TXE, OUTPUT);

  delay(100);

  digitalWrite(SOC_GPIO_PIN_TXE, HIGH);

  delay(100);

  pinMode(SOC_GPIO_PIN_TXE, INPUT);
}

static void STM32_WDT_setup()
{
  // Init the watchdog timer with 5 seconds timeout
  IWatchdog.begin(5000000);
}

static void STM32_WDT_fini()
{
  /* once emabled - there is no way to disable WDT on STM32 */

  if (IWatchdog.isEnabled()) {
    IWatchdog.set(IWDG_TIMEOUT_MAX);
  }
}

static void STM32_Button_setup()
{
  /* TODO */
}

static void STM32_Button_loop()
{
  /* TODO */
}

static void STM32_Button_fini()
{
  /* TODO */
}

#if defined(USBD_USE_CDC)

#include <USBSerial.h>

static void STM32_USB_setup()
{
#if defined(DISABLE_GENERIC_SERIALUSB)
  SerialUSB.begin();
#endif
}

static void STM32_USB_loop()
{

}

static void STM32_USB_fini()
{
  /* TBD */
}

static int STM32_USB_available()
{
  return SerialUSB.available();
}

static int STM32_USB_read()
{
  return SerialUSB.read();
}

static size_t STM32_USB_write(const uint8_t *buffer, size_t size)
{
  return SerialUSB.write(buffer, size);
}

IODev_ops_t STM32_USBSerial_ops = {
  "STM32 USBSerial",
  STM32_USB_setup,
  STM32_USB_loop,
  STM32_USB_fini,
  STM32_USB_available,
  STM32_USB_read,
  STM32_USB_write
};

#endif /* USBD_USE_CDC */

const SoC_ops_t STM32_ops = {
  SOC_STM32,
  "STM32",
  STM32_setup,
  STM32_post_init,
  STM32_loop,
  STM32_fini,
  STM32_reset,
  STM32_getChipId,
  STM32_getResetInfoPtr,
  STM32_getResetInfo,
  STM32_getResetReason,
  STM32_getFreeHeap,
  STM32_random,
  STM32_Sound_test,
  NULL,
  STM32_WiFi_set_param,
  STM32_WiFi_transmit_UDP,
  NULL,
  NULL,
  NULL,
  STM32_EEPROM_begin,
  STM32_SPI_begin,
  STM32_swSer_begin,
  STM32_swSer_enableRx,
  NULL, /* STM32 has no built-in Bluetooth */
#if defined(USBD_USE_CDC)
  &STM32_USBSerial_ops,
#else
  NULL,
#endif
  STM32_Display_setup,
  STM32_Display_loop,
  STM32_Display_fini,
  STM32_Battery_setup,
  STM32_Battery_voltage,
  STM32_GNSS_PPS_Interrupt_handler,
  STM32_get_PPS_TimeMarker,
  STM32_Baro_setup,
  STM32_UATSerial_begin,
  STM32_UATModule_restart,
  STM32_WDT_setup,
  STM32_WDT_fini,
  STM32_Button_setup,
  STM32_Button_loop,
  STM32_Button_fini
};

#endif /* ARDUINO_ARCH_STM32 */
