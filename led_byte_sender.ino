#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 6
#define NUM_PIXELS 240

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

//#define TWO_BITS_PER_PIXEL 1
//#define FOUR_BITS_PER_PIXEL 1
#define EIGHT_BITS_PER_PIXEL 1
#define BAUD_RATE 300
#define MAX_BUFFER 100
#define STRIP_SPEED_MULTIPLE 1

#ifdef TWO_BITS_PER_PIXEL
const uint8_t MAX_PIXEL_OFFSET = 11;
const int ms_per_pixel_move = STRIP_SPEED_MULTIPLE * 1000 / BAUD_RATE;
uint32_t colours[2] = { strip.Color(0, 0, 0x22), strip.Color(0xdd, 0xdd, 0x00) };
#endif
#ifdef FOUR_BITS_PER_PIXEL
const uint8_t MAX_PIXEL_OFFSET = 2;
const int ms_per_pixel_move = STRIP_SPEED_MULTIPLE * 4 * 1000 / BAUD_RATE;
uint32_t colours[16] = {
  strip.Color(0x80,0x80,0x00), // 0x0: olive
  strip.Color(0x00,0x80,0x00), // 0x1: dark green
  strip.Color(0x80,0x00,0x00), // 0x2: maroon
  strip.Color(0xa5,0x2a,0x2a), // 0x3: brown
  strip.Color(0xff,0xa5,0x00), // 0x4: orange
  strip.Color(0x80,0x00,0x80), // 0x5: purple
  strip.Color(0x80,0x80,0x00), // 0x6:
  strip.Color(0x00,0x80,0x80), // 0x7:
  strip.Color(0x80,0x80,0x80), // 0x8: grey
  strip.Color(0x00,0x00,0xff), // 0x9: bright blue
  strip.Color(0xff,0x00,0x00), // 0xa: bright red
  strip.Color(0x00,0xff,0x00), // 0xb: bright green
  strip.Color(0xff,0x00,0xff), // 0xc: purple
  strip.Color(0x00,0xff,0xff), // 0xd: cyan
  strip.Color(0xff,0xff,0x00), // 0xe: yellow
  strip.Color(0xff,0xff,0xff), // 0xf: white
};
#endif
#ifdef EIGHT_BITS_PER_PIXEL
const uint8_t MAX_PIXEL_OFFSET = 1;
const int ms_per_pixel_move = STRIP_SPEED_MULTIPLE * 8 * 1000 / BAUD_RATE;
#endif

const uint16_t STRIP_LENGTH_IN_BYTES = 0;

struct unsettableByte {
  bool set;
  uint8_t value;
} incoming_byte;

struct unsettableByte strip_bytes[MAX_BUFFER];
uint16_t first_byte_buffer_pos = 0;
uint16_t last_byte_buffer_pos = 0;

uint8_t pixel_offset = 0;


// INTERRUPT on OPTO PIN
// read serial data if 'ready to receive'
// throw new byte onto end of 'displayed buffer'
//   - displayed buffer ==
//

void setup() {
  Serial.begin(BAUD_RATE);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
}

long last_byte_read_millis = 0;
long last_strip_update_millis = 0;
long loop_count = 0;

void populateSerialBuffer() {
  // buffer has not been read
  if ( incoming_byte.set == true ) {
    //Serial.println("Byte already set!");
    return;
  }

  // we can read another byte
  if (Serial.available() > 0) {
    incoming_byte.value = Serial.read();
    incoming_byte.set = true;
    //Serial.println(incoming_byte.value);
  }

}

struct unsettableByte blank_byte = { false, 0 };

void updateStripBytes(bool set, uint8_t value) {
  first_byte_buffer_pos++;
  first_byte_buffer_pos %= MAX_BUFFER;
  strip_bytes[first_byte_buffer_pos].set = set;
  if ( set ) {
    strip_bytes[first_byte_buffer_pos].value = value;
  }
}

bool byteNeededOnStrip() {
  if ( pixel_offset > MAX_PIXEL_OFFSET )
    return true;
  else
    return false;
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

uint32_t convertByteToColor(uint8_t a) {
  uint8_t r,g,b;
  if ( a == 0 ) {
    r = 2; g = 0; b = 0;
  } else {
    r = ( a & 0b11110000 );
    g = ( a & 0b00111100 ) << 2;
    b = ( a & 0b00001111 ) << 4;
  }
  return strip.Color(r,g,b);
}

void updateStrip() {
  for (uint16_t i=strip.numPixels(); i>0; i--) {
    // REVELATION: We only need to know what the first pixel should be!
    //  - rest just shift along with getPixelColor()
    //  - but we need to set in reverse (pixel N..1)
    bool byte_is_set = strip_bytes[first_byte_buffer_pos].set;
    uint8_t val = strip_bytes[first_byte_buffer_pos].value;
    if ( i-1 == 0 ) {
#ifdef TWO_BITS_PER_PIXEL
      if ( pixel_offset == 0 || pixel_offset >= 9 ) {
        strip.setPixelColor(i-1, 0); // blank
      } else {
        uint8_t bit_to_read = (8 - pixel_offset);
        strip.setPixelColor(i-1, colours[1]);
        if ( byte_is_set ) {
          strip.setPixelColor(i-1, colours[bitRead(val, bit_to_read)]);
        } else {
          strip.setPixelColor(i-1, 0);
        }
      }
#endif
#ifdef FOUR_BITS_PER_PIXEL
      if ( pixel_offset >= 2 || ! byte_is_set ) {
        strip.setPixelColor(i-1, 0); // blank
      } else if ( pixel_offset == 0 ) {
        uint8_t nibble = val >> 4; // most significant nibble
        strip.setPixelColor(i-1, colours[nibble]);
      } else if ( pixel_offset == 1 ) {
        uint8_t nibble = val & 0x0f; // least significant nibble
        strip.setPixelColor(i-1, colours[nibble]);
      }
#endif
#ifdef EIGHT_BITS_PER_PIXEL
      if ( pixel_offset == 0 && byte_is_set ) {
        strip.setPixelColor(i-1, convertByteToColor(val));
      } else {
        strip.setPixelColor(i-1, 0); // blank
      }
#endif
    } else {
      strip.setPixelColor(i-1, strip.getPixelColor(i-2)); // hopefully not slow :/
    }
  }
  strip.show();
  pixel_offset++;
}

void sendFallingByteAsSerial() {
  // send the 'falling off' byte out via Serial
  uint16_t falling_byte_buffer_pos;
  if ( first_byte_buffer_pos < STRIP_LENGTH_IN_BYTES ) {
    falling_byte_buffer_pos = MAX_BUFFER + first_byte_buffer_pos - STRIP_LENGTH_IN_BYTES;
  } else {
    falling_byte_buffer_pos = first_byte_buffer_pos - STRIP_LENGTH_IN_BYTES;
  }
  /*
  Serial.print(first_byte_buffer_pos);
  Serial.print("/");
  Serial.write(strip_bytes[first_byte_buffer_pos].value);
  Serial.print(" -- ");
  Serial.print(falling_byte_buffer_pos);
  Serial.print("/ ");
  Serial.write(strip_bytes[falling_byte_buffer_pos].value);
  Serial.println();
  */
  bool falling_byte_set = strip_bytes[falling_byte_buffer_pos].set;
  uint8_t falling_byte_value = strip_bytes[falling_byte_buffer_pos].value;
  if ( falling_byte_set ) {
    Serial.write(falling_byte_value);
  } else {
    //Serial.write('*');
    //Serial.print("unset");
  }
  //Serial.println("");
}

void loop() {
  //Serial.println("LoopStart");
  populateSerialBuffer();
  if ( byteNeededOnStrip() ) {
    if ( incoming_byte.set ) {
      updateStripBytes(true, incoming_byte.value);
      incoming_byte.set = false;
      pixel_offset=0; // we have a new byte to display
    } else {
      updateStripBytes(false, 0);
    }
    sendFallingByteAsSerial();
  }
  if ( timeToRefreshStrip() ) {
    updateStrip();
  }
  if ( loop_count == 100000 ) {
    loop_count = 0;
  }
  loop_count++;
  //Serial.print("millis: "); Serial.println(millis());
  //Serial.println("LoopEnd");
}

