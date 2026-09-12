#pragma once
#include <stdint.h>

namespace display {
    bool begin();
    void loop();
    void setBrightness(uint8_t v);   // 0-255

    // Swipe gesture detection — polled once per loop(), consumes on read.
    // swipe_from_top: finger started in top 50px and moved down ≥70px
    // swipe_up:       finger moved up ≥70px (more vertical than horizontal)
    // swipe_left:     finger moved left ≥70px (more horizontal than vertical)
    bool consume_swipe_from_top();
    bool consume_swipe_up();
    bool consume_swipe_left();
}
