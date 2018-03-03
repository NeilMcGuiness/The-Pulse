/*
  ______ __             ____          __          
 /_  __// /_   ___     / __ \ __  __ / /_____ ___ 
  / /  / __ \ / _ \   / /_/ // / / // // ___// _ \
 / /  / / / //  __/  / ____// /_/ // /(__  )/  __/
/_/  /_/ /_/ \___/  /_/     \__,_//_//____/ \___/

Developed on the....
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

//%%%%%%%%%%%%%%%%%  Coded by Neil Robert Mcguiness in 2017-2018 %%%%%%%%%%%%%%%
*/
#include <Bela.h>
#include <Midi.h>
#include <stdlib.h>
#include <rtdk.h>
#include <cmath>
#include <fstream>
#include <OSCServer.h>
#include <WriteFile.h>

#define MAX_ONSETS 8
#define MAX_COARSE_ONSETS 4
#define TRACK_MODE 1
#define TAP_MODE 0

//LED variables
bool LEDstate = false;
bool onFlag = false;
int LEDTimeOut = 0;
//-------------------
// System Status variables
static int status=GPIO_LOW;
static int pulseMode;
//-------------------------
// Onset, timeout and BPM adjustment variables
bool trig = false;
bool enoughTrackTaps = false;
bool enoughCoarseTaps = false;
bool enoughTaps = false;
int timeOutsamples;
int timeOutCount = 0;
int digitalSampleRate;
int count;
int tapCount = 0;
int coarseTaps = 0;
int sampleInterval; // miliseconds for the clock pulse (24 PPQN = Pulses per quater note)
int bpmIncrement; 
int frames; // frames to count through so that the Bpm can be incremented every quater note.
int tapIndex = 0;
int onsetInd = 0;
float lastTap = 0.f;
float now;
float timer;
float oneMs;
float bpm = 120;
float movingBPM;
float gSamplingPeriod = 0;
float taps[4];
float onsets[MAX_ONSETS];
//--------------------------------
// Switching Flags ################################
bool coarseOn = false;
bool trackOn = false;
//--------------------------------

///%%%%% SENSOR LOGGING VARIABLES %%%%%%%%%%%%%%%%%
float seconds = 0.f;
float input = 0.f;
int measuredSamps = 0;
//--------------------------------

// Coarse Tempo variables
bool resetFlag = false;
int timerIndex = 0;
int samplesSinceLastTap = 0;
float timerArray[MAX_COARSE_ONSETS];
float averageIOI;
float msSinceLastTap = 0;
// -------------------

// Tempo Adjustment variables
int durationAsEighthNotes;
float tempoThreshold = 0.9;
float tempoStdDev = 50;
float alpha = 0.7; // system responisiveness (how fast tempo changes are made), I' making this 1.0 for now.
// -------------------

// Phase Sync variables
int beatPos = -1; // initialising as -1 until enough taps are reached
int beatOffset = 0;
float syncThreshold;
float syncStdDev = 50; // beggining with 50 ms.
float beta = 1.0; // similar to alpha but for the sync process.
float syncDelta;
float discrepencies[MAX_ONSETS];
float mostRecentMidiClickTime = 0;
float closestMidiClickTime = 0;
// -------------------

// Probability Weights
float tempoWeights[16] = {0.9, 1.0, 0.1, 1.0, 0.1, 0.1, 0.1, 0.8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // Duration in eight notes (2 is a quater note, 4 is a half note etc.)
float syncWeights[8] = {1.0, 0.1, 1.0, 0.4, 1.0, 0.4, 1.0, 0.4}; // Eighth note beats in the bar.
// -------------------

// Standard Metrical Divisions (ms) 
// *** Will be recalulated upon tempo changes ****
float eightNote;
float quarterNote;
float halfNote;
float wholeNote;

// &&&&&&&&&&&&&&& Functions &&&&&&&&&&&&&&&&&&&&&&&
void tempoTrack();
void tempoAdjust();
void syncAdjust();
void calculateStandardNoteDivisions(float newBpm);
float gaussianTempo (float error);
float gaussianSync (float discrepency);
void discrepencyCalculation(float timeNow);
//----------------------------------
// Midi variables
Midi midi;
const char* gMidiPort0 = "hw:1,0,0";
//----------------------------------

// &&&&&&&&&&&& SETUP %%%%%%%%%%%%%%%%%%%%%
bool setup(BelaContext *context, void *userData)
{
	midi.readFrom(gMidiPort0);
	midi.writeTo(gMidiPort0);
	midi.enableParser(true);
	oneMs = context->audioSampleRate / 1000.0;
	digitalSampleRate = context->digitalSampleRate;
	calculateStandardNoteDivisions(bpm);

	for(int t = 0; t < 4; t ++) // Resetting the timer array to null.
	{
		timerArray[t] = 0.0;
	}
	
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

	gSamplingPeriod = 1.0 /context->audioSampleRate;
	sampleInterval = ((60000 / bpm) / 24) * 44.1; // equation to determine the miliseconds needed per pulse.
										// ...Because Midi clock needs 24 pulses per quaternote (PPQ)
										// then I multiply by 44.1 to give the result in samples.
	// midi_byte_t startByte = 250;
	// midi.writeOutput(startByte);
	pinMode(context, 0, P8_07, OUTPUT); // LED for TAP_MODE
	pinMode(context, 0, P8_08, INPUT); // footswitch
	pinMode(context, 0, P8_09, OUTPUT); // LED for TRACK_MODE

	return true;
}

// %%%%%%% RENDER LOOP %%%%%%%%%%%%%%%%%%%%%
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
void render(BelaContext *context, void *userData)
{
	for(unsigned int n = 0; n < context->analogFrames; n++)
	{
		float piezo = analogRead(context, n, 6); // reading the piezo value to detect Kick onsets..
		samplesSinceLastTap++;
		msSinceLastTap = (samplesSinceLastTap / context->audioSampleRate) * 1000.0;
		seconds = context->audioFramesElapsed / context->audioSampleRate;
		pulseMode = digitalRead(context, 0, P8_08); // Reading the state of the footswitch.
		
		if(pulseMode == TAP_MODE)
		{
			if(coarseOn == false)
			{
				coarseOn = true;
				coarseTaps = 0;
				trackOn = false;
				digitalWrite(context, n, P8_09, 0);
				tapCount = 0;
				enoughCoarseTaps = false;
			}
		}
		
		else if(pulseMode == TRACK_MODE)
		{
			if(trackOn == false)
			{
				trackOn = true;
				coarseOn = false;
				digitalWrite(context, n, P8_07, 0);
			}
		}

		if(trig == true)
		{
			timeOutCount++;
		}
		
		if(msSinceLastTap >= 4000.0 && resetFlag == false) // Been a long time since recent onset so will start the process of collecting onsets and comparing again.
		{
			resetFlag = true;
			tapCount = 0;
			timerIndex = 0;
			beatPos = -1;
			enoughTrackTaps = false;
			enoughCoarseTaps = false;
			enoughTaps = false;
			status = GPIO_HIGH;
			if(pulseMode == TAP_MODE)
			{
			digitalWrite(context, n, P8_07, status); //Switching LED back on to indicate no Tempo Tracking.
			}
			else if (pulseMode == TRACK_MODE)
			{
			digitalWrite(context, n, P8_09, status);	
			}
			
			rt_printf("TOO LONG SINCE LAST TAP, RESET\n");

			for(int t = 0; t < 4; t ++) // Resetting the timer array to null.
			{
				timerArray[t] = 0.0;
			}
		}
		
		if(piezo > 0.3) // ONSET DETECTED.
		{
			if(trig == false) // if not already triggered.
			{
				msSinceLastTap = 0;
				samplesSinceLastTap = 0;
				trig = true;
				resetFlag = false;
				now = (context->audioFramesElapsed / context->audioSampleRate) * 1000; // getting the time NOW (* 1000 for ms?).
				timer = now - lastTap; // working out difference between now and the last tap.
				lastTap = now; // updating the last tap to THIS tap.
				onsets[onsetInd] = now; // placing onset CPU time in ring buffer.
				onsetInd = (onsetInd + 1) % MAX_ONSETS; // incrementing and wrapping around.

				tapCount ++;
				
				float summedIOI = 0.0;
				int elapsedTaps = tapCount;
					if(tapCount > MAX_COARSE_ONSETS)
				{
					elapsedTaps = MAX_COARSE_ONSETS;
				}

				timerArray[timerIndex] = timer;
				timerIndex = (timerIndex + 1) % MAX_COARSE_ONSETS;
				
				for(int i = 0; i < MAX_COARSE_ONSETS; i ++) // Obtaining average IOI time between quarter pulses.
				{
					summedIOI += timerArray[i];
				}
				
				averageIOI = summedIOI / elapsedTaps; // Calculating the Coarse BPM assesmnt at this stage.
				
				if(tapCount >= 5) // If we have enough relevent recent onsets to compare to then we will execute the algorithm.
				{
					// execute the main algorithms.
					if(pulseMode == TRACK_MODE)
					{
						syncAdjust();
						tempoAdjust(); 
						enoughTrackTaps = true;
					}
					else if (pulseMode == TAP_MODE)
					{
						bpm = 60000 / averageIOI;
						sampleInterval = ((60000 / bpm) / 24) * 44.1;
						calculateStandardNoteDivisions(bpm);
						rt_printf("TAP ADJUSTMENT, 	BPM estimate = %f\n", bpm);
						enoughCoarseTaps = true;
					}
					
					if(enoughTaps == false)
						{
							enoughTaps = true;
							midi_byte_t startByte = 250;
							midi.writeOutput(startByte);
							rt_printf("MIDI START MESSAGE\n");
						}
				}
				else if (tapCount < 5)
				{
					if(pulseMode == TRACK_MODE)
					{
						bpm = 60000 / timer; // or / timer?
						sampleInterval = ((60000 / bpm) / 24) * 44.1;
						calculateStandardNoteDivisions(bpm);
						rt_printf("Coarse ADJUSTMENT, 	BPM estimate = %f\n", bpm);
					}
				}
				
				
				
			} // End of piezo trigger brace.
		} // End of piezo threshold brace.
		
		if (piezo < 0.05) // Setting up for retriggering if fallen below the low threshold.
		{
			if (trig == true && timeOutCount > 5000 ) // if it has been triggered and Timeout is completed then untrigger it.
			{
				trig = false;
				timeOutCount = 0;
			}
		}
	}
	
	for(unsigned int n=0; n<context->digitalFrames; n++) // LED handling section.
	{
		if(pulseMode == TRACK_MODE)
		{
			if(enoughTrackTaps) // If enough taps then we have a BPM estimate and can start tempo tracking.
			{

					if (onFlag) // This is triggered every quarter note (in the audio frames loop)
					{
						if(status == GPIO_LOW)
						{
						    status=GPIO_HIGH; // Switch Light on to coincide with quarternote pulse.
							digitalWrite(context, n, P8_09, status); //write the status to the LED
						}
						
						timeOutCount++;
    					if(timeOutCount == timeOutsamples)
    					{
    						status=GPIO_LOW; // Switch LED off after timeout phase.	
    						digitalWrite(context, n, P8_09, status); //write the status to the LED
							timeOutCount = 0;
							onFlag = false;
    					}
					}
	
			}
		
			else // If not enough taps then LED is steadily on just to show signs of life.
			{
				 status = GPIO_HIGH; // Switch Light on to coincide with quarternote pulse.
				 digitalWrite(context, n, P8_09, status); //write the status to the LED
			}
		}
		
		else if (pulseMode == TAP_MODE)
		{
			if(enoughCoarseTaps) // If enough taps then we have a BPM estimate and can start tempo tracking.
			{

					if (onFlag) // This is triggered every quarter note (in the audio frames loop)
					{
						if(status == GPIO_LOW)
						{
						    status=GPIO_HIGH; // Switch Light on to coincide with quarternote pulse.
							digitalWrite(context, n, P8_07, status); //write the status to the LED
						}
						
						timeOutCount++;
    					if(timeOutCount == timeOutsamples)
    					{
    						status=GPIO_LOW; // Switch LED off after timeout phase.	
    						digitalWrite(context, n, P8_07, status); //write the status to the LED
							timeOutCount = 0;
							onFlag = false;
    					}
					}
	
			}
			
			else // If not enough taps then LED is steadily on just to show signs of life.
			{
				 status = GPIO_HIGH; // Switch Light on to coincide with quarternote pulse.
				 digitalWrite(context, n, P8_07, status); //write the status to the LED
			}
		}
	}
	
	for(unsigned int n = 0; n < context->audioFrames; n++)
	{
		count ++; // count up each sample.

		if(!(count % (int)(oneMs * eightNote))) // Every EighthNote.
		{
			mostRecentMidiClickTime = (context->audioFramesElapsed / context->audioSampleRate) * 1000; // Getting the time NOW.

			if(enoughTrackTaps)
			{
				beatPos ++;
				if(beatPos > 7)
				{
					beatPos = 0;
				}

			}
			
		}
		
		if(!(count % sampleInterval)) // when the samplecount reaches the calculated number to represent a 24th of a quater pulse.
		{
			midi_byte_t clockPulse = 248; // Midi byte is set to decimal 248 (Midid devices recognise this as clock pulse)
			midi.writeOutput(clockPulse); // Send the pulse to the device.
			frames ++; // increment the number of frames
			if (frames == 24) // when we've reached a whole quaternote
			{
				frames = 0;
				onFlag = true;
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
void tempoAdjust()
{
	float IOIs[MAX_ONSETS];
	float PEs[MAX_ONSETS];
	float summedPEs = 0;
	float PEsMean;
	float PEsCumDifs = 0;
	int periodDurations[MAX_ONSETS];
	float accuracies[MAX_ONSETS];
	int mostRecent;
	float tempoDelta = 0.f;
	float oldBpm;
	
	if(onsetInd == 0)
	{
		mostRecent = MAX_ONSETS - 1;
	}
	else
	{
	 	mostRecent = (onsetInd - 1) % MAX_ONSETS;
	}
	
	for(int k = 0; k < MAX_ONSETS; k ++) // processing For Loop.
	{
		// Getting IOI between current and older onsets. ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		int newIndex = (onsetInd + k) % MAX_ONSETS; // Logic to get the other onsets from oldest to newest.
		
		IOIs[k] = onsets[mostRecent] - onsets[newIndex]; 
		rt_printf("IOI[%d] = %f 	(now = %f)   - ('Then' = %f) \n", k, IOIs[k],onsets[mostRecent], onsets[newIndex]);
		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		// Classifying the IOI as a regular period Duration (in eighth notes) @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		
		periodDurations[k] = round(IOIs[k] / eightNote); // the round function gives us the closest regular duration that the IOI represents (in eigthnotes).
		//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		
		// Determining the Performance Error between the actual IOI and the regular duration it is closest to.
		// &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
		PEs[k] = IOIs[k] - (periodDurations[k] * eightNote);
		summedPEs += PEs[k];
		//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
		// Calculating overall accuracy  **********************************************************
		
		/* Accuracy is determined by feeding the performance error of the IOI (between current and kth previous onset)
		   Into a Gaussian window and then scaling this result with a weight dependent on the determined periodDuration */
		if(periodDurations[k] < 16 && periodDurations[k] > 0)
		{
		accuracies[k] = gaussianTempo(PEs[k]) * tempoWeights[periodDurations[k] - 1];  // -1 because the first index of tempoWeights[] should represent 1 eighth note and not 0 eighth notes.
		}
		else
		{
			accuracies[k] = 0.f;
		}
		// *****************************************************************************************
		rt_printf("periodDuration[%d] = %d 		PE[%d] = %f \n Accuracy[%d] = %f 	Gaussian result = %f	Tempoweight[%d] = %.2f\n", k, periodDurations[k], k, PEs[k], k, accuracies[k], gaussianTempo(PEs[k]), periodDurations[k] -1, tempoWeights[periodDurations[k] - 1]);
	} // End of processing For Loop
	
	PEsMean = fabs(summedPEs / (MAX_ONSETS - 1));
	
	// Finding Most accurate "winning" IOI. (minimum Performance error)
	int win = 0; // This is the winningIndex.
	float mostAccurate = accuracies[0];
	for (int i = 0; i < MAX_ONSETS - 1; i++) // IS THE -1 NECESSARY?
	{
		if(accuracies[i] > mostAccurate && accuracies[i] <= 1.0 && periodDurations[i] != 0 && periodDurations[i] < 16) // Second part of condition is to ensure that the closest period duration is not 0.
		{
			mostAccurate = accuracies[i];
			win = i;
		}
		PEsCumDifs += powf((PEs[i] - PEsMean), 2);
	}
	// rt_printf("Winning Onset = %d with accuracy %f\n", win, mostAccurate);
	//Finally compare the most accuracies to the threshold and if its close enough then we update the tempo!
	
	if (mostAccurate > tempoThreshold)
	{
		// This is how much the tempo needs to change and is determined by using the winning IOI data.
		tempoDelta = alpha * gaussianTempo(PEs[win]) * tempoWeights[periodDurations[win] - 1] * (PEs[win] / (periodDurations[win]));
		
			if (mostAccurate >= tempoThreshold + 0.1) // If most accurate is over the threshold AND the headroom then update the threshold.
		{
			tempoThreshold = tempoThreshold + (0.3 * (mostAccurate - tempoThreshold - 0.1));
			if(tempoThreshold > 0.99)
			{
				tempoThreshold = 0.99;
			}
		}
		
	}
	
		else if (mostAccurate <= tempoThreshold) // If the most accurate is less than the threshold then we lower the threshold.
	{
		tempoThreshold *= 0.6;
		if(tempoThreshold < 0.2)
		{
			tempoThreshold = 0.2;
		}
		tempoDelta = 0.f;

	}

	oldBpm = bpm;
	bpm = bpm + ((tempoDelta * -1.0) + syncDelta); // This is where the Bpm/tempo is updated. If in sync then the syncDelta variable will be 0.
	
	if(bpm < 60.f) // constraining the bpm extremes.
	{
		bpm = 60.f;
	}
	
	if(bpm > 240.f)
	{
		bpm = 240.f;
	}
	calculateStandardNoteDivisions(bpm);
	sampleInterval = ((60000 / bpm) / 24) * 44.1; // Calculating how many samples for the Midi Pulse (24 pulses per quarterNote).
	rt_printf("TRACK ADJUSTMENT Bpm = %f 	newBpm = %f\n", oldBpm, bpm);
	rt_printf("Tempo Threshold = %f 	TempoStdDev = %f 	TempoDelta = %f\n", tempoThreshold, tempoStdDev,tempoDelta);

	// // And the final parameter to update is the tempoStdDev which pivots around an equilibrium point of 0.7..
	tempoStdDev = fabs(PEsCumDifs / PEsMean);
	tempoStdDev = tempoStdDev * (1 + ((0.7 * tempoWeights[periodDurations[win]]) - mostAccurate));
	if(tempoStdDev > 2000.0)
	{
		tempoStdDev = 2000.0;
	}
} // End of Tempo Process.

void syncAdjust()
{
	float discrepency;
	float proximityToExpected;
	float discrepencySum = 0;
	float discrepencyMean = 0;
	float discrepencyCumDifs = 0;
	
	int newBeatPos = (beatPos + beatOffset) % sizeof(syncWeights);
	
	discrepency = now - closestMidiClickTime;
	
	discrepencies[onsetInd] = discrepency;
	
	if (fabs(discrepency) < 100) // If within 100 ms of an expected Beat.
	{
		proximityToExpected = gaussianSync(discrepency) * syncWeights[newBeatPos];
		
		// If the onset is close to the expected beat (but not so close!) then we syncronise.
		if (proximityToExpected > syncThreshold && proximityToExpected < (syncThreshold + 0.1)) 
		{
			syncDelta = (((gaussianSync(discrepency) + beta) / (beta + 1)) * gaussianSync(discrepency) * syncWeights[newBeatPos] * discrepency);
		}
		
    	else if (proximityToExpected > (syncThreshold + 0.1)) // We are close enough to the beat so no need for Sync.
    	{
    		syncDelta = 0; // reset the sync amount to 0 so that it does not affect the tempo adjustment.
    		
    		// Raise the threshold.
    		syncThreshold = syncThreshold + (0.3 * (proximityToExpected - syncThreshold - 0.1));
    		// syncStdDev *= 0.9; // Narrowing the gaussian window (GUESSWORK VARIABLE AT THIS STAGE)
   	
    	}
    	
    	else if (proximityToExpected < syncThreshold)
    	{
    		// lower the threshold (closer to the threshold has a more dramatic effect)
    		syncThreshold = syncThreshold * (1 - gaussianSync(discrepency));
    		// syncStdDev *= 1.3; // Widening the gaussian window (GUESSWORK VARIABLE AT THIS STAGE)
    	}
	} 
	
	for(int k = 0; k < MAX_ONSETS; k ++)
	{
		 discrepencySum += discrepencies[k];
	}
	
	discrepencyMean = discrepencySum / MAX_ONSETS;
	
	for(int k = 0; k < MAX_ONSETS; k ++)
	{
		 discrepencyCumDifs += powf((discrepencies[k] - discrepencyMean), 2);
	}
	
	if (tapCount > MAX_ONSETS) // only update when we have a large enough dataset.
	{
	syncStdDev = discrepencyCumDifs/discrepencyMean;
	}
}

void calculateStandardNoteDivisions(float newBpm)
{
	quarterNote = 60000 / bpm;
	eightNote = quarterNote / 2;
	halfNote = quarterNote * 2;
	wholeNote = quarterNote * 4;
	
	timeOutsamples = ((digitalSampleRate / 1000) * quarterNote) / 4;
}

float gaussianSync (float discrepency)
{
	float exponent = (std::pow((discrepency), 2) / (2 * syncStdDev)) * -1; // Don't need to square standardDev as the variance (squared StdDev) is calculated from dataset.
	return std::exp(exponent);
}

float gaussianTempo (float error)
{

	float exponent = (std::pow((error), 2) / (2 *tempoStdDev)) * -1;
	return std::exp(exponent);
}

void discrepencyCalculation(float timeNow)
{
	float timeElapsedSinceLastMidiClick = timeNow - mostRecentMidiClickTime;
	
	if (timeElapsedSinceLastMidiClick > (eightNote / 2))
	{
		closestMidiClickTime = mostRecentMidiClickTime + eightNote;
		beatOffset = 1;
	}
	else
	{
		closestMidiClickTime = mostRecentMidiClickTime;	
		beatOffset = 0;
	}
}
// End Project - 
//%%%%%%%%%%%%%%%%%  Coded by Neil Robert Mcguiness in 2017-2018 %%%%%%%%%%%%%%%