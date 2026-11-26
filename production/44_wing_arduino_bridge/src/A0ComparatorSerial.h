#pragma once
#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "WingBridgeWire.h"

// A0 is the negative input. The comparator inverts the ESP's 0/3.3 V
// waveform around the internal reference, giving normal UART polarity.
class A0ComparatorSerial {
 public:
  void begin() {
    pinMode(A0, INPUT);
    PRR &= ~_BV(PRADC);
    ADCSRA &= ~_BV(ADEN);
    ADMUX = 0; // ADC0 is A0.
    ADCSRB |= _BV(ACME);
    DIDR0 |= _BV(ADC0D);
    ACSR = _BV(ACBG) | _BV(ACIC) | _BV(ACI);
    delay(2); // Allow the internal reference to settle before capturing edges.

    // Timer1 runs freely at 16 MHz. Compare A schedules receive samples.
    TIMSK1 = 0;
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    TCCR1B = _BV(CS10); // Capture the falling (start) edge.
    listen();
  }

  bool available() const { return head_ != tail_; }

  int read() {
    const uint8_t tail = tail_;
    if (tail == head_) return -1;
    const uint8_t value = buffer_[tail];
    tail_ = (tail + 1) & BufferMask;
    return value;
  }

  bool overflow() {
    bool lost;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      lost = badByte_;
      if (lost) {
        tail_ = head_;
        badByte_ = false;
      }
    }
    return lost;
  }

  void startBit() {
    const uint16_t start = ICR1;
    if (uint16_t(TCNT1 - start) >= BitTicks / 2) {
      // An edge captured during a WS2811 transfer may have arrived too late.
      badByte_ = true;
      return;
    }
    bit_ = 0;
    value_ = 0;
    OCR1A = start + BitTicks / 2;
    TIFR1 = _BV(OCF1A);
    TIMSK1 = _BV(OCIE1A);
  }

  void sample() {
    const bool high = ACSR & _BV(ACO);
    if (bit_ == 0 && high) {
      listen(); // A glitch ended before the middle of the start bit.
      return;
    }
    if (bit_ == 9) {
      // Re-arm capture before enqueueing so the next start edge can be latched
      // even if it arrives while we are still finishing this interrupt.
      listen();
      if (high) enqueue();
      else badByte_ = true; // Invalid stop bit.
      return;
    }
    if (bit_ != 0) value_ = (value_ >> 1) | (high ? 0x80 : 0);
    ++bit_;
    OCR1A += BitTicks;
  }

 private:
  static_assert(F_CPU == 16000000UL, "A0 receiver requires a 16 MHz ATmega328P");
  static constexpr uint16_t BitTicks = (F_CPU + WingBridge::Baud / 2) / WingBridge::Baud;
  static constexpr uint8_t BufferMask = 63;

  void listen() {
    TIFR1 = _BV(ICF1);
    TIMSK1 = _BV(ICIE1);
  }

  void enqueue() {
    const uint8_t next = (head_ + 1) & BufferMask;
    if (next == tail_) {
      badByte_ = true;
      return;
    }
    buffer_[head_] = value_;
    head_ = next;
  }

  volatile uint8_t buffer_[BufferMask + 1]{};
  volatile uint8_t head_ = 0, tail_ = 0;
  volatile bool badByte_ = false;
  uint8_t bit_ = 0, value_ = 0;
};

extern A0ComparatorSerial link;
ISR(TIMER1_CAPT_vect) { link.startBit(); }
ISR(TIMER1_COMPA_vect) { link.sample(); }
