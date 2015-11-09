#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 6
#define NUM_PIXELS 120

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, PIN, NEO_GRB + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

// SUPPOSE we have 1 pixel per 4 bits: 16 colours, plus blank for 'no data'
//   - F - white (stands out, looks 'high')
//   - 1-E: mix (any colours that look 'different' from white+blue)
//        - 0x7E should be distinctive (PPP)
//   - 0 - blue (stands out, looks 'low')
// THEN we have NUM_PIXELS * 4 bits per strip
//  which at 240 pixels: 960 bits per strip: or ~3s latency for 300 baud
//                                           or ~0.8s for 1200 baud
//   - 120 bytes on strip
//  which at 120 pixels: 480 bits per strip: or ~1.5s latency for 300 baud
//                                           or ~0.4s for 1200 baud
//   - 60 bytes on strip

// Also could have a 'binary' mode:
//  1 pixel per bit:
//  240 pixels: 240 pixels per strip: 0.8s latency per strip
//   - 30 bytes on strip
//  120 pixels: 120 pixels per strip: 0.4s latency per strip
//   - 15 bytes on strip
// could 'colour' key bits/bytes:
//  * PPP headers; 0x00; 0xFF; 0x7E
//  * HDLC/PPP bit-stuffed packets? -- 111110 in data stream
//

#define TWO_BITS_PER_PIXEL 1
//#define FOUR_BITS_PER_PIXEL 1
#define BAUD_RATE 300
#define STRIP_LATENCY_MS 1000
#define MAX_BUFFER 120

#ifdef TWO_BITS_PER_PIXEL
uint32_t colours[2] = { strip.Color(0, 0, 0xff), strip.Color(0xff, 0xff, 0xff) };
#endif
#ifdef FOUR_BITS_PER_PIXEL
uint32_t colours[16];
#endif

struct unsettableByte {
  bool set;
  uint8_t value;
} incoming_byte;

const int ms_per_pixel_move = STRIP_LATENCY_MS / NUM_PIXELS; // for 1s latency
struct unsettableByte strip_bytes[MAX_BUFFER];
uint8_t first_byte_buffer_pos = 0;
uint8_t last_byte_buffer_pos = 0;

uint8_t offset = 0;

// INTERRUPT on OPTO PIN
// read serial data if 'ready to receive'
// throw new byte onto end of 'displayed buffer'
//   - displayed buffer ==
//

void setup() {
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
}

long last_byte_read_millis;
long last_strip_update_millis;

void populateNextByte() {
  // buffer has not been read
  if ( incoming_byte.set == true )
    return;

  // we can read another byte
  if (Serial.available() > 0) {
    incoming_byte.value = Serial.read();
    incoming_byte.set = true;
  }

}

uint8_t bufferDiff(uint8_t start, uint8_t end) {
  if ( start > end ) {
    return end + MAX_BUFFER - start;
  } else {
    return end - start;
  }
}

bool shift_buffer_bytes = false;
struct unsettableByte blank_byte = { false, 0 };

void updateStripBytes(bool set, uint8_t value) {
  first_byte_buffer_pos++;
  first_byte_buffer_pos %= MAX_BUFFER;
  strip_bytes[first_byte_buffer_pos].set = set;
  strip_bytes[first_byte_buffer_pos].value = value;
}

bool byteNeededOnStrip() {
  return true; // TODO: how do we know that we need a new byte?
}

bool timeToRefreshStrip() {
  long now = millis();
  if ( now - last_strip_update_millis > ms_per_pixel_move ) {
    last_strip_update_millis = now;
    return true;
  } else {
    return false;
  }
}

uint8_t bit_offset = 0;

void updateStrip() {
  // 'first' byte is coming onto strip
  // 'last' byte is exiting strip
  // at bit_offset == 0, first byte is entirely off strip,
  //                     last byte is potentially leaving it.
  // at bit_offset == 1, MSB first bit of first byte enters strip
  //
  // each 'byte' is 11 pixels: 8 pixels data, 3 pixels trailing blank
  for (uint16_t i=strip.numPixels()-1; i>=0; i--) {
    // REVELATION: We only need to know what the first pixel should be!
    //  - rest just shift along with getPixelColor()
    //  - but we need to set in reverse.
    // calculate which bit to set
    //  - 11 pixels per byte
    //    so at offset:
    //     0: i==0 => blank
    //     1: i==0 => bitRead(value, 7)
    //     2: i==0 => bitRead(value, 6)
    //     3: i==0 => bitRead(value, 5)
    //     4: i==0 => bitRead(value, 4)
    //     5: i==0 => bitRead(value, 3)
    //     6: i==0 => bitRead(value, 2)
    //     7: i==0 => bitRead(value, 1)
    //     8: i==0 => bitRead(value, 0)
    //     9: i==0 => blank
    //    10: i==0 => blank
    uint8_t val = strip_bytes[first_byte_buffer_pos].value;
    if ( i == 0 ) {
      if ( bit_offset == 0 || bit_offset == 9 || bit_offset == 10 ) {
        strip.setPixelColor(i, 0); // blank
      } else {
        uint8_t bit_to_read = (8 - offset);
        if ( strip_bytes[first_byte_buffer_pos].set ) {
          strip.setPixelColor(i, colours[bitRead(val, bit_to_read)]);
        } else {
          strip.setPixelColor(i, 0);
        }
      }
    } else {
      strip.setPixelColor(i, strip.getPixelColor(i-1)); // hopefully not slow :/
    }

  }
}

void loop() {
  populateNextByte();
  if ( byteNeededOnStrip() ) {
    if ( incoming_byte.set ) {
      updateStripBytes(true, incoming_byte.value);
      incoming_byte.set = false;
    } else {
      updateStripBytes(false, 0);
    }
  }
  if ( timeToRefreshStrip() ) {
    updateStrip();
  }

}

