/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

The platform for ultra-low latency audio and sensor processing

http://bela.io

A project of the Augmented Instruments Laboratory within the
Centre for Digital Music at Queen Mary University of London.
http://www.eecs.qmul.ac.uk/~andrewm

(c) 2016 Augmented Instruments Laboratory: Andrew McPherson,
  Astrid Bin, Liam Donovan, Christian Heinrichs, Robert Jack,
  Giulio Moro, Laurel Pardue, Victor Zappi. All rights reserved.

The Bela software is distributed under the GNU Lesser General Public License
(LGPL 3.0), available here: https://www.gnu.org/licenses/lgpl-3.0.txt
*/


#include <Bela.h>
#include <Midi.h>
#include <stdlib.h>
#include <rtdk.h>
#include <cmath>

float gFreq;
float gPhaseIncrement = 0;
bool gIsNoteOn = 0;
int gVelocity = 0;
int count;
int sampleInterval; // miliseconds for the clock pulse
int bpmIncrement; 
int bpm = 70;
float miliseconds = 500;
int frames; // frames to count through so that the Bpm can be incremented every quater note.
float gSamplingPeriod = 0;
float taps[4];
float lastTap = 0.f;
float now;
float timer;
int tapIndex = 0;
int timerIndex = 0;


/*
 * This callback is called every time a new input Midi message is available
 *
 * Note that this is called in a different thread than the audio processing one.
 *
 */
void midiMessageCallback(MidiChannelMessage message, void* arg){
	if(arg != NULL){
		rt_printf("Message from midi port %s ", (const char*) arg);
	}
	message.prettyPrint();
	if(message.getType() == kmmNoteOn){
		gFreq = powf(2, (message.getDataByte(0)-69)/12.0f) * 440;
		gVelocity = message.getDataByte(1);
		gPhaseIncrement = 2 * M_PI * gFreq * gSamplingPeriod;
		gIsNoteOn = gVelocity > 0;
		rt_printf("v0:%f, ph: %6.5f, gVelocity: %d\n", gFreq, gPhaseIncrement, gVelocity);
	}
}

bool trig = false;

Midi midi;

const char* gMidiPort0 = "hw:1,0,0";

bool setup(BelaContext *context, void *userData)
{
	midi.readFrom(gMidiPort0);
	midi.writeTo(gMidiPort0);
	midi.enableParser(true);
	midi.setParserCallback(midiMessageCallback, (void*) gMidiPort0);
	if(context->analogFrames == 0) {
		rt_printf("Error: this example needs the analog I/O to be enabled\n");
		return false;
	}

	if(context->audioOutChannels < 2 ||
		context->analogOutChannels < 2){
		printf("Error: for this project, you need at least 2 analog and audio output channels.\n");
		return false;
	}

	gSamplingPeriod = 1/context->audioSampleRate;
	sampleInterval = (60000 / bpm / 24) * 44.1; // equation to determine the miliseconds needed per pulse.
										// ...Because Midi clock needs 24 pulses per quaternote (PPQ)
										// then I multiply by 44.1 to give the result in samples.
	midi_byte_t startByte = 250;
	midi.writeOutput(startByte);
	return true;
}

// render() is called regularly at the highest priority by the audio engine.
// Input and output are given from the audio hardware and the other
// ADCs and DACs (if available). If only audio is available, numMatrixFrames
// will be 0.


enum {kVelocity, kNoteOn, kNoteNumber};
void render(BelaContext *context, void *userData)
{
	
	for(unsigned int n = 0; n < context->analogFrames; n++)
	{
		float piezo = analogRead(context, n, 0); // reading the piezo value.
		
		if(piezo > 0.3)
		{
			if(trig == false) // if not already triggered.
			{
				trig = true;
				rt_printf("TRIGGER with reading of %.4f || Timer = %.4f", piezo, timer);
				now = context->audioFramesElapsed / context->audioSampleRate; // getting the time NOW.
				timer = now - lastTap; // working out difference between now and the last tap.
				lastTap = now; // updating the last tap to THIS tap.
			}
			// If it is already triggered then we do nothing (trigger and hold)
		}
		
		if (piezo < 0.05) // Setting up for retriggering if fallen below the low threshold.
		{
			if (trig == true) // if it has been triggered then untrigger it.
			{
				trig = false;
			}
		}
		
		
	}

	for(unsigned int n = 0; n < context->audioFrames; n++)
	{
		count ++; // count up each sample.
		
		// Run DTW.predict
		// Get classification prediction;
		// if 1 (onset), then get time of onset & compare time dif to last onset (giving the intended rhythm)
		// Average several of these onsets to get a more stable bpm.
		// Update the sampleInterval using the formula SI = (60000/ bpm/ 24) * 44.12
		
		/* Potential improvements:- the maximumlikelihood of the Classificaton in percentage could
		   determine the 'weight' of the detected onset, with more 'confident' onsets having more
		   influence over the beat tracking than less confident onsets.*/
		
		if(!(count % sampleInterval)) // when the samplecount reaches the calculated number to represent a 24th of a quater pulse.
		{
			count = 0; // counter is rest.
			midi_byte_t clockPulse = 248; // Midi byte is set to decimal 248 (Midid devices recognise this as clock pulse)
			midi.writeOutput(clockPulse); // Send the pulse to the device.
			frames ++; // increment the number of frames
			if (frames == 24) // when we've reached a whole quaternote
			{
				frames = 0; //reset the frames.
				bpm ++; // and increment the BPM by 1.
				sampleInterval = (60000 / bpm / 24) * 44.1; // and then recalculate the number of samples needed to represent this new bpm in pulses.
			}
		}
	}
}

// cleanup() is called once at the end, after the audio has stopped.
// Release any resources that were allocated in setup().

void cleanup(BelaContext *context, void *userData)
{
	midi_byte_t stopByte = 252;
	midi.writeOutput(stopByte);
}

/**
\example 05-Communication/MIDI/render.cpp

Connecting MIDI devices to Bela!
-------------------------------

This example needs documentation.

*/
