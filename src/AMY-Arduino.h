// AMY-Arduino.h
// connector for Arduino

#ifndef AMYARDUINOH
#define AMYARDUINOH

#ifdef ARDUINO_ARCH_RP2040
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico-audio/audio_i2s.h"
#include "pico/binary_info.h"
#endif

#include "Arduino.h"

extern "C" {
  #include "amy.h"
  #include "examples.h"
}


class AMY
{
  public:
    AMY();
    void begin();
    void begin_multicore();

    int32_t sysclock();
    void fm(int32_t start);
    void drums(int32_t start, uint16_t loops);
    void reset();
    struct event default_event();
    void add_event(struct event e);
    void send_message(char * message);
    void volume(float vol);

    void prepare();
    void restart();
    void render(uint16_t start, uint16_t end, uint8_t core);
    int16_t * fill_buffer();
    int16_t * render_to_buffer();
};

#endif




