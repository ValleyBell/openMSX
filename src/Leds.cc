// $Id$

#include <iostream>
#include "Leds.hh"
#include "openmsx.hh"


namespace openmsx {

Leds::Leds()
{
	pwrLed = capsLed = kanaLed = pauseLed = turboLed = true;
	fddLedCounter=1;
	setLed(POWER_OFF);
	setLed(CAPS_OFF);
	setLed(KANA_OFF);
	setLed(PAUSE_OFF);
	setLed(TURBO_OFF);
	setLed(FDD_OFF);
}

Leds::~Leds()
{
	setLed(POWER_OFF);
	setLed(CAPS_OFF);
	setLed(KANA_OFF);
	setLed(PAUSE_OFF);
	setLed(TURBO_OFF);
	if (fddLedCounter>0) setLed(FDD_OFF);
}

Leds* Leds::instance()
{
	static Leds oneInstance;
	return &oneInstance;
}


void Leds::setLed(LEDCommand led)
{
	switch (led) {
	case POWER_ON:
		if (!pwrLed) {
			PRT_INFO ("Power LED ON");
			pwrLed = true;
		}
		break;
	case POWER_OFF:
		if (pwrLed) {
			PRT_INFO ("Power LED OFF");
			pwrLed = false;
		}
		break;
	case CAPS_ON:
		if (!capsLed) {
			PRT_INFO ("Caps LED ON");
			capsLed = true;
		}
		break;
	case CAPS_OFF:
		if (capsLed) {
			PRT_INFO ("Caps LED OFF");
			capsLed = false;
		}
		break;
	case KANA_ON:
		if (!kanaLed) {
			PRT_INFO ("Kana LED ON");
			kanaLed = true;
		}
		break;
	case KANA_OFF:
		if (kanaLed) {
			PRT_INFO ("Kana LED OFF");
			kanaLed = false;
		}
		break;
	case PAUSE_ON:
		if (!pauseLed) {
			PRT_INFO ("Pause LED ON");
			pauseLed = true;
		}
		break;
	case PAUSE_OFF:
		if (pauseLed) {
			PRT_INFO ("Pause LED OFF");
			pauseLed = false;
		}
		break;
	case TURBO_ON:
		if (!turboLed) {
			PRT_INFO ("Turbo LED ON");
			turboLed = true;
		}
		break;
	case TURBO_OFF:
		if (turboLed) {
			PRT_INFO ("Turbo LED OFF");
			turboLed = false;
		}
		break;
	case FDD_ON:
		if (fddLedCounter==0) { // turn on if it was off
			PRT_INFO ("FDD LED ON");
		}
		fddLedCounter++;
		break;
	case FDD_OFF:
		if (fddLedCounter==1) { // only turn off when it is the last one
			PRT_INFO ("FDD LED OFF");
		}
		fddLedCounter--;
		break;
	}
}

} // namespace openmsx
