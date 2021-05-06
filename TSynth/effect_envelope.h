/* Audio Library for Teensy 3.X
 * Portions Copyright (c) 2017, Paul Stoffregen, paul@pjrc.com
 * Portions Copyright (c) 2021 Vince R. Pearson
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef effect_envelope_h_
#define effect_envelope_h_
#include "Arduino.h"
#include "AudioStream.h"
#include "utility/dspinst.h"

#define SAMPLES_PER_MSEC (AUDIO_SAMPLE_RATE_EXACT/1000.0)
#define EXP_ENV_ONE ((uint32_t)0x80000000)
// Envelope type. EXP_Nx are attacks with different amounts of negative curvature. EXP_Px have positive curvature.
// EXP_0 has a linear attack.
#define NUM_ENV_TYPES 18 // Linear, Exp -8 through Exp +8

// Precalculate these filter constants
// These are used to allow fast sustain control change with long decay values.
#define FAST_SUSTAIN_KF 0.999773268 // 100 ms
//#define FAST_SUSTAIN_KF 0.999886627 // 200 ms
//#define FAST_SUSTAIN_KF 0.999943312 // 400 ms
//#define FAST_SUSTAIN_KF 0.999971655 // 800 ms
#define FAST_SUSTAIN_K1 (uint32_t)(FAST_SUSTAIN_KF*EXP_ENV_ONE)
#define FAST_SUSTAIN_K2 (1.0-FAST_SUSTAIN_KF)

// The exponential difference equations are based on the impulse invariant method. 
// The equation used for attack is k1 = exp(curveFactor*Ts/To) and k2=(k1-1)/((e^curveFactor)-1)
// and ynew = k1*yold+k2 (normalized).
// This allows different eponential curvatures (positive and negative) with the endpoints constrained
// to start at 0 and end at 1 (normalized) in the specified time interval.
// The funtion for the k2 term is a continous funtion of curveFactor through zero, but it is an indeterminate form at zero that
// This causes problems with finite precision math when approaching zero from either side (like the sinc funtion sin(x)/x but worse).
// It can be expressed as a continous function using a ratio of Taylor series but it is simpler to restrict the values to integers
// between -8 and 8 treating zero as a special case (linear ramp).



class AudioEffectEnvelopeTS : public AudioStream
{
public:
	  AudioEffectEnvelopeTS() : AudioStream(1, inputQueueArray) {
		state = 0;
    env_type=-128; // Added for addition of exponential envelope. Default is linear. -8 through 8 are different amounts of positive and negative curvature.
		delay(0.0f);  // default values...
		attack(10.5f);
		hold(2.5f);
		decay(35.0f);
		sustain(0.5f);
		release(300.0f);
		releaseNoteOn(5.0f);
	}
	void noteOn();
	void noteOff();
	FLASHMEM void delay(float milliseconds) {
		delay_count = milliseconds2count(milliseconds); // Number of samples is 8 times this number for linear mode.
	}

 // Attack curve negative numbers generate negative curvature (like typical exp attack curves).
 // Positive numbers generate positive curvature (accelerating upward).
 // Zero produces linear attack curve.
 // This is used to change the time constant and target value to enforce zero to full scale in
 // the requested attack time.
 // The equation used for the input, (1-e^(curve_factor*Ts/To))(e^curve_factor-1) is singular at curve_factor=0.
 // The precision required starts to blow up as abs(curve_factor) approaches zero at which there is a singularity.
 // The function is however continuous through zero and can be expressed as a ratio of power series but it is easier to restrict the
 // curve factors to be integers and to treat zero as a special case which is a linear ramp.
 // The numbers are additionally restricted to -8 to +8 as numbers outside this range asymptotically approach a step function.

  FLASHMEM void setEnvType(int8_t type)
  {
    env_type=type;
    updateExpAttack(); // Exponential filter parameters need updating.
  }
  FLASHMEM int8_t getEnvType() { return env_type;};

 
	FLASHMEM void attack(float milliseconds) {
		attack_count = milliseconds2count(milliseconds);
		updateExpAttack();
    
	}
	FLASHMEM void hold(float milliseconds) {
		hold_count = milliseconds2count(milliseconds);
	}
	FLASHMEM void decay(float milliseconds) {
		decay_count = milliseconds2count(milliseconds);
		if (decay_count == 0) decay_count = 1;
    updateExpDecay();
	}
	FLASHMEM void sustain(float level) {
		if (level < 0.0) level = 0;
		else if (level > 1.0) level = 1.0;
		sustain_mult = level * 1073741824.0;
    sustain_target=level*EXP_ENV_ONE; // max level is 0x80000000 for exp, 0x40000000 for lin.
    updateExpDecay(); // Change in sustain requires recalulation of decay target
    // Change of sustain level while in decay or regular sustain state forces state change so sustain changes occur more quickly.
    __disable_irq();
    if(state==STATE_DECAY || state==STATE_SUSTAIN)
      state=STATE_SUSTAIN_FAST_CHANGE;
    __enable_irq();
	}
	FLASHMEM void release(float milliseconds) {
		release_count = milliseconds2count(milliseconds);
		if (release_count == 0) release_count = 1;
    updateExpRelease();
	}
  void releaseNoteOn(float milliseconds) {
		release_forced_count = milliseconds2count(milliseconds);
		if (release_count == 0) release_count = 1;
    updateExpReleaseNoteOn();
	}
 //ElectroTechnique 2020 - close the envelope to silence it
void close(){
 __disable_irq();
     mult_hires = 0;//Zero gain
     inc_hires = 0;//Same as STATE_DELAY
     state=STATE_IDLE; // Added, VRP. Shouldn't this be in the idle state when closed?
     ysum=0;
  __enable_irq();
}
	bool isActive();
	bool isSustain();
	using AudioStream::release;
	virtual void update(void);
  void setEnvType(uint8_t type);
private:
	uint16_t milliseconds2count(float milliseconds) {
		if (milliseconds < 0.0) milliseconds = 0.0;
		uint32_t c = ((uint32_t)(milliseconds*SAMPLES_PER_MSEC)+7)>>3;
		if (c > 65535) c = 65535; // allow up to 11.88 seconds
		return c;
	}
  FLASHMEM void updateExpDecay() // needed in case sustain changes.
  {
    double k;
    k=exp(-1.0L/(decay_count*4.0L));
    decay_k=(uint32_t)(EXP_ENV_ONE*k);
    decay_target=(1.0L-k)*sustain_target;
  }
  
  FLASHMEM void updateExpAttack() // This is needed in case env type changes.
  {
   double k1,k2; 
    if(env_type>8 || env_type<-8) return; // Anything outside this range is a linear envelope.
    if(env_type==0) // Linear attack. Everything else is exponential.
    {
      attack_k=EXP_ENV_ONE;
      attack_target=(uint32_t)(EXP_ENV_ONE/(attack_count*8.0L));
    }
    else
    {
      k1=exp((double)env_type/(attack_count*8.0L));
      k2=(k1-1.0L)/(exp((double)env_type)-1.0L);
      attack_k=(uint32_t)(EXP_ENV_ONE*k1);
      attack_target=(uint32_t)(EXP_ENV_ONE*k2);
    }
  }

  FLASHMEM void updateExpRelease()
  {
    double k;
    k=exp(-1.0L/(release_count*4.0L));
    release_k=(uint32_t)(EXP_ENV_ONE*k);
  }

  FLASHMEM void updateExpReleaseNoteOn()
  {
    double k;
    k=exp(-1.0L/(release_forced_count*4.0L));
    release_forced_k=(uint32_t)(EXP_ENV_ONE*k);
  }

	audio_block_t *inputQueueArray[1];
  static const uint8_t env_type_values[NUM_ENV_TYPES];
	// state
	uint8_t  state;      // idle, delay, attack, hold, decay, sustain, release, forced (now idle_next)
	uint16_t count;      // how much time remains in this state, in 8 sample units
	int32_t  mult_hires; // attenuation, 0=off, 0x40000000=unity gain
	int32_t  inc_hires;  // amount to change mult_hires every 8 samples

	// settings
	uint16_t delay_count;
	uint16_t attack_count;
	uint16_t hold_count;
	uint16_t decay_count;
	int32_t  sustain_mult;
	uint16_t release_count;
	uint16_t release_forced_count;

  // Exponential ADSR variables
  enum { // make this a private class enum set instead of using defines.
    STATE_IDLE,
    STATE_DELAY,
    STATE_ATTACK,
    STATE_HOLD,
    STATE_DECAY,
    STATE_SUSTAIN,
    STATE_SUSTAIN_FAST_CHANGE,
    STATE_RELEASE,
    STATE_FORCED,
    STATE_IDLE_NEXT,  // Not used for original linear envelope.
    };
  int8_t env_type; // Attack curve type. Limit to -8 to 8 integers for exp curve, -128 for linear.
  uint32_t exp_count;
  uint32_t ysum;
  uint32_t exp_mult[8]; // Calculate multipliers 8 at a time.
  uint32_t attack_k;
  uint32_t attack_target;
  uint32_t decay_k;
  uint32_t decay_target;
  uint32_t sustain_target;
  uint32_t release_k;
  uint32_t release_forced_k;
};

#undef SAMPLES_PER_MSEC
#endif
