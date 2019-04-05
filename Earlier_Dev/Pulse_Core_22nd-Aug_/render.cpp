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
#include <fstream>
#include <OSCServer.h>
#define MAX_ONSETS 4

OSCServer oscServer;
AuxiliaryTask getOsc;

bool LEDstate = false;
int LEDTimeOut = 0;
int count;
int tapCount = 0;
int rowCount = 0;
int sampleInterval; // miliseconds for the clock pulse
int bpmIncrement; 
float bpm = 120;
// float miliseconds = 500;
int frames; // frames to count through so that the Bpm can be incremented every quater note.
float gSamplingPeriod = 0;
float taps[4];
float onsets[MAX_ONSETS];
int onsetInd = 0;
float lastTap = 0.f;
float now;
float timer;
int tapIndex = 0;
int timerIndex = 0;
int timeOutCount = 0;

// *** Standard Metrical Divisions (ms) *****
// ###############################################
// *** Will be recalulated upon tempo changes ****

float eightNote;
float quarterNote;
float halfNote;
float wholeNote;

bool trig = false;
void oscCallback();
void tempoTrack();
void calculateStandardNoteDivisions(float newBpm);
void gaussian (float newOnset, float expectedOnset);

Midi midi;
const char* gMidiPort0 = "hw:1,0,0";

// &&&&&&&&&&&& SETUP %%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
bool setup(BelaContext *context, void *userData)
{
	oscServer.setup(7562); // Setting up to read from juce app.
	midi.readFrom(gMidiPort0);
	midi.writeTo(gMidiPort0);
	midi.enableParser(true);
	calculateStandardNoteDivisions(bpm);
	
	getOsc = Bela_createAuxiliaryTask(oscCallback, 90, "getOsc"); // Creating aux task to read the Osc Message.
	
	if(context->analogFrames == 0) 
	{
		rt_printf("Error: this example needs the analog I/O to be enabled\n");
		return false;
	}

	if(context->audioOutChannels < 2 ||
		context->analogOutChannels < 2)
	{
		printf("Error: for this project, you need at least 2 analog and audio output channels.\n");
		return false;
	}

	gSamplingPeriod = 1/context->audioSampleRate;
	sampleInterval = ((60000 / bpm) / 24) * 44.1; // equation to determine the miliseconds needed per pulse.
										// ...Because Midi clock needs 24 pulses per quaternote (PPQ)
										// then I multiply by 44.1 to give the result in samples.
	midi_byte_t startByte = 250;
	midi.writeOutput(startByte);
	pinMode(context, 0, P8_07, OUTPUT);

	return true;
}

// %%%%%%% RENDER LOOP %%%%%%%%%%%%%%%%%%%%%
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
void render(BelaContext *context, void *userData)
{
	
	for(unsigned int n = 0; n < context->analogFrames; n++)
	{
		float piezo = analogRead(context, n, 7); // reading the piezo value.
		if(trig == true)
		{
			timeOutCount++;
		}
		
		if(piezo > 0.3) // ONSET DETECTED.
		{
			if(trig == false) // if not already triggered.
			{
				trig = true;
				now = (context->audioFramesElapsed / context->audioSampleRate) * 1000; // getting the time NOW (* 1000 for ms?).
				timer = now - lastTap; // working out difference between now and the last tap.
				lastTap = now; // updating the last tap to THIS tap.
				
				// rt_printf("TRIGGER with reading of %.4f || Timer = %.4f\n", piezo, timer);

				rt_printf("Now = : %f\n", now);
				onsets[onsetInd] = now; // placing onset CPU time in ring buffer.
				onsetInd = (onsetInd + 1) % MAX_ONSETS; // incrementing and wrapping around.
				
				
					for (int i = 0; i < MAX_ONSETS; i ++) // Printing out the buffer of onset times.
            		{
                		rt_printf("Index : %d  Value : %f\n", i, onsets[i]);
            		}
	
				if(timer >= 2000) // Been a long time since recent onset so will start the process of collecting onsets and comparing again.
				{
					tapCount = 0;
				}
				else // If IOI between now and most recent onset is shorter than 2 seconds then increment the tapCount as normal.
				{
					tapCount ++;
				}
				
				rt_printf("TapCount = %d :: onsetInd = %d \n\n", tapCount, onsetInd);

				
				if(tapCount >= MAX_ONSETS) // If we have enough relevent recent onsets to compare to then we will execute the algorithm.
				{
					tempoTrack(); // execute the main algorithm.
				}
			}
			// If it is already triggered then we do nothing (trigger and hold)
		}
		
		if (piezo < 0.05) // Setting up for retriggering if fallen below the low threshold.
		{
			if (trig == true && timeOutCount > 5000 ) // if it has been triggered and Timeout is completed then untrigger it.
			{
				trig = false;
				timeOutCount = 0;
			}
		}
	}
	
	for(unsigned int n=0; n<context->digitalFrames; n++)
	{
		// digitalWriteOnce(context, n, P8_07, LEDstate); //write the status to the LED
	}
	

	for(unsigned int n = 0; n < context->audioFrames; n++)
	{
		count ++; // count up each sample.
		if(LEDstate)
		{
			LEDTimeOut++;
			if(LEDTimeOut >= 15000)
			{
				LEDstate = 0;
				digitalWrite(context, n, P8_07, LEDstate); //write the status to the LED
			}
		}
		
		if(!(count % 44)) // Roughly every second.
		{
			if(oscServer.messageWaiting())
			{
			Bela_scheduleAuxiliaryTask(getOsc);	
			}
		}
		
		
		if(!(count % sampleInterval)) // when the samplecount reaches the calculated number to represent a 24th of a quater pulse.
		{
			count = 0; // counter is rest.
			midi_byte_t clockPulse = 248; // Midi byte is set to decimal 248 (Midid devices recognise this as clock pulse)
			midi.writeOutput(clockPulse); // Send the pulse to the device.
			frames ++; // increment the number of frames
			if (frames == 24) // when we've reached a whole quaternote
			{
				LEDstate = 1;
				digitalWrite(context, n, P8_07, LEDstate); //write the status to the LED
				frames = 0;
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

// The main tempo tracking algorithm, called from the main thread when an onset is detected (with enough recent onsets to be relevent).
void tempoTrack()
{
	// onsets[onsetInd] = newOnset; // placing onset CPU time in ring buffer.
	// onsetInd = (onsetInd + 1) % MAX_ONSETS; // incrementing and wrapping around.
		// rt_printf("Current Bpm = %f\n", bpm);

	
	
	float IOIs[MAX_ONSETS];
	float PEs[MAX_ONSETS];
	int mostRecent;
	
	if(onsetInd == 0)
	{
		mostRecent = MAX_ONSETS - 1;
	}
	
	else
	{
	 	mostRecent = (onsetInd - 1) % MAX_ONSETS;
	}
	
	for(int i = 0; i < MAX_ONSETS; i ++)
	{
		 int newIndex = (onsetInd + i) % MAX_ONSETS; // Logic to get the other onsets from oldest to newest.

		// Work out difference between current onset and previous ones.
		IOIs[i] = onsets[mostRecent] - onsets[newIndex]; 
		rt_printf("IOI[%d] = %f 	(now = %f)   - ('Then' = %f) \n", i, IOIs[i],onsets[mostRecent], onsets[newIndex]);

	}
	
	// Then categorise these IOI as divisions that closest match the expected divisions of the current tempo.
	
	for(int i = 0; i < MAX_ONSETS - 1; i ++) // Categorisation logic (- 1 because MAX_ONSETS-1 will always be 0 (the inter onset interval between the most recent entry and itself!)
	{
		float difference = 0;
		 if (IOIs[i] >= (eightNote / 2) && IOIs[i] <= ((eightNote + quarterNote) / 2))
		 { // Then we have an eightNote
		 	// Calculate the Performance Error.
		 	difference = IOIs[i] - eightNote;
		 	PEs[i] = (difference / eightNote) * 100;
		 	// PEs[i] = fabs(PEs[i]);
		 }
		 
		 else if (IOIs[i] >= ((eightNote + quarterNote) / 2) && IOIs[i] <= ((quarterNote + halfNote) / 2))
		 { // Then we have a quarterNote.
		 	difference = IOIs[i] - quarterNote;
		 	PEs[i] = (difference / quarterNote) * 100;
			// PEs[i] = fabs(PEs[i]);
		 }
		 
		  else if (IOIs[i] >= ((quarterNote + halfNote) / 2) && IOIs[i] <= ((halfNote + (quarterNote * 3)) / 2))
		 { // Then we have a halfNote..
		 	difference = IOIs[i] - halfNote;
		 	PEs[i] = (difference / halfNote) * 100;
		 	// PEs[i] = fabs(PEs[i]);
		 }
		 
		 else if (IOIs[i] >= ((halfNote + (quarterNote * 3)) / 2) && IOIs[i] <= (((quarterNote * 3) + wholeNote) / 2))
		 { // Then we have a 3-quarter note (Dotted Half)
		 	difference = IOIs[i] - (3 * quarterNote);
		 	PEs[i] = (difference / (3 * quarterNote)) * 100;
		 	// PEs[i] = fabs(PEs[i]);
		 }
		 
		  else if (IOIs[i] >= (((quarterNote * 3) + wholeNote) / 2) && IOIs[i] <= ((wholeNote +(halfNote * 3)) / 2))
		 { // Then we have a wholeNote.
			difference = IOIs[i] - wholeNote;
		 	PEs[i] = (difference / wholeNote) * 100;	
		 	// PEs[i] = fabs(PEs[i]);
		 }
	}
	
	// Finding closest match (minimum Performance error)
	float smallest = fabs(PEs[0]);
	for (int i = 0; i < MAX_ONSETS - 1; i++)
	{
		if(fabs(PEs[i]) < smallest)
		{
			smallest = PEs[i];
		}
	}
	
	rt_printf("performance error = : %f \n", smallest);
	// rt_printf("PEs[0] = %f\nPEs[1] =  %f \nPEs[2] = %f \n\n", PEs[0], PEs[1], PEs[2]);
	
	// Adjust Tempo by the minimum performance error percentage (flipped**).
	/* ** IOIs that were longer (slower) than the expected division would give a positive percentage,
		  but a slower performance needs a LOWER BPM therefore the percentage to adjust the BPM needs
		  to be negative, and vise versa.*/
		  
	if(fabs(smallest) > 3.0) // accuracy threshold
	{
		float oldBpm = bpm;
		float bpmOnePercent = bpm / 100;
		float errorAdjustment = (bpmOnePercent * smallest) * -1;

		
		// bpm = bpm + ((bpm / 100) * -smallest);
		bpm = bpm + errorAdjustment;
		sampleInterval = ((60000 / bpm) / 24) * 44.1;
		calculateStandardNoteDivisions(bpm);
		rt_printf("Bpm = %f 	newBpm = %f\n", oldBpm, bpm);
		// rt_printf("ADJUSTMENT\n");
	}
	
	// rt_printf("Current Bpm = %f\n", bpm);
}

void calculateStandardNoteDivisions(float newBpm)
{
	quarterNote = 60000 / bpm;
	eightNote = quarterNote / 2;
	halfNote = quarterNote * 2;
	wholeNote = quarterNote * 4;
}

void gaussian (float newOnset, float expectedOnset)
{
	float variance; // How to determine this???
	float exponent = (std::pow((newOnset - expectedOnset), 2) / variance) * -1;
	std::exp(exponent);
}

void oscCallback()
{
	oscpkt::Message msg;
	
	msg = oscServer.popMessage();
	float floatArg;
	float floatArg2;
	
	if (msg.match("/osc-test").popFloat(floatArg).popFloat(floatArg2).isOkNoMoreArgs()) // This should tell us if the sim is running?
    {
    	rowCount++; // Only if sim is running.

   		if(!(rowCount % 50) && rowCount > 1000) // Every 20 ms We'll use the pipeline to predict (classify).
   		{
   
   		}
    }
}


