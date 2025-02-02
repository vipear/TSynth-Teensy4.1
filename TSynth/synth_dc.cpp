/* Audio Library for Teensy 3.X
 * Portions Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 * Portions Copyright (c) 2021, Vince R. Pearson (exponential glide functionality)
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

#include <Arduino.h>
#include "synth_dc.h"

void AudioSynthWaveformDcTS::update(void)
{
  audio_block_t *block;
  uint32_t *p, *end, val;
  int32_t count, t1, t2, t3, t4;

  block = allocate();
  if (!block) return;
  p = (uint32_t *)(block->data);
  end = p + AUDIO_BLOCK_SAMPLES/2;

  if (state == 0) {
    mode=modePending;
    // steady DC output, simply fill the buffer with fixed value
    if(mode!=GLIDE_EXP)
      val = pack_16t_16t(magnitude, magnitude);
    else
      val=pack_16t_16t(expMagnitude<<2,expMagnitude<<2);
   
    do {
      *p++ = val;
      *p++ = val;
      *p++ = val;
      *p++ = val;
      *p++ = val;
      *p++ = val;
      *p++ = val;
      *p++ = val;
    } while (p < end);
  } else if(mode!=GLIDE_EXP) 
  {
    // transitioning to a new DC level
    //count = (target - magnitude) / increment;
    count = substract_int32_then_divide_int32(target, magnitude, increment);
    if (count >= AUDIO_BLOCK_SAMPLES) {
      // this update will not reach the target
      do {
        magnitude += increment;
        t1 = magnitude;
        magnitude += increment;
        t1 = pack_16t_16t(magnitude, t1);
        magnitude += increment;
        t2 = magnitude;
        magnitude += increment;
        t2 = pack_16t_16t(magnitude, t2);
        magnitude += increment;
        t3 = magnitude;
        magnitude += increment;
        t3 = pack_16t_16t(magnitude, t3);
        magnitude += increment;
        t4 = magnitude;
        magnitude += increment;
        t4 = pack_16t_16t(magnitude, t4);
        *p++ = t1;
        *p++ = t2;
        *p++ = t3;
        *p++ = t4;
      } while (p < end);
    } else {
      // this update reaches the target
      while (count >= 2) {
        count -= 2;
        magnitude += increment;
        t1 = magnitude;
        magnitude += increment;
        t1 = pack_16t_16t(magnitude, t1);
        *p++ = t1;
      }
      if (count) {
        t1 = pack_16t_16t(target, magnitude + increment);
        *p++ = t1;
      }
      magnitude = target;
      state = 0;
      mode=modePending;
      val = pack_16t_16t(magnitude, magnitude);
      while (p < end) {
        *p++ = val;
      }
    }
  }
  else // Exponential glide. Vince R. Pearson
  do
  {

    ysum=ysum+(int64_t)kf*(expTargetB-expMagnitude);
    t1=ysum>>32;
    
    ysum=ysum+(int64_t)kf*(expTargetB-t1);
    t2=ysum>>32;

    ysum=ysum+(int64_t)kf*(expTargetB-t2);
    t3=ysum>>32;
    
    ysum=ysum+(int64_t)kf*(expTargetB-t3);
    t4=ysum>>32;
    expMagnitude=t4;

    // Check for target crossing starting with last sample. If a sample has not hit target
    // all previous samples have not either so skip further checks.
    if((target-t4)*stepDirection<0)
    {
      t4=expTarget;
      state=0;
      expMagnitude=expTargetB=expTarget;
      if((expTarget-t3)*stepDirection<0)
      {
        t3=expTarget;
        if((expTarget-t2)*stepDirection<0)
        {
          t2=expTarget;
          if((expTarget-t1)*stepDirection<0)
            t1=expTarget;
        }
      } 
    }
    t1= pack_16t_16t(t1<<2, t2<<2);
    t2= pack_16t_16t(t3<<2, t4<<2);

    *p++ = t1;
    *p++ = t2;
  } while(p<end);
  transmit(block);
  release(block);
}
