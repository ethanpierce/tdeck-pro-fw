# Display protocol provenance

The native ESP-IDF display driver in `src/native_screen.cpp` adapts the
GDEQ031T10 / UC8253 command protocol from Jean-Marc Zingg’s GxEPD2,
as carried by LILYGO (the previously vendored 1.5.5 subset):

https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/53bcfa593b1df064894a8efc5a7e15e4524b09d2/lib/GxEPD2

The original GPL-3.0 license is retained here. The Arduino library source and
its Adafruit GFX runtime dependency have been removed. The native driver uses
the same panel settings and OTP refresh commands, with ESP-IDF SPI and GPIO.
