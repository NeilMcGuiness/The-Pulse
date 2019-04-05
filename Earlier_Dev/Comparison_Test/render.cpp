/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

 _________  ___   ___   ______       ______   __  __   __       ______   ______      
/________/\/__/\ /__/\ /_____/\     /_____/\ /_/\/_/\ /_/\     /_____/\ /_____/\     
\__.::.__\/\::\ \\  \ \\::::_\/_    \:::_ \ \\:\ \:\ \\:\ \    \::::_\/_\::::_\/_    
   \::\ \   \::\/_\ .\ \\:\/___/\    \:(_) \ \\:\ \:\ \\:\ \    \:\/___/\\:\/___/\   
    \::\ \   \:: ___::\ \\::___\/_    \: ___\/ \:\ \:\ \\:\ \____\_::._\:\\::___\/_  
     \::\ \   \: \ \\::\ \\:\____/\    \ \ \    \:\_\:\ \\:\/___/\ /____\:\\:\____/\ 
      \__\/    \__\/ \::\/ \_____\/     \_\/     \_____\/ \_____\/ \_____\/ \_____\/ 
      ==============================================================================
      Patch by Neil McGuiness - created to Log sensor data derived froma drum performance that will be
    							guided by a generated click track that changes over time (Tempo map). 

 
   _____                            __  __              
 |_   _|__ _ __ ___  _ __   ___   |  \/  | __ _ _ __   
   | |/ _ \ '_ ` _ \| '_ \ / _ \  | |\/| |/ _` | '_ \  
   | |  __/ | | | | | |_) | (_) | | |  | | (_| | |_) | 
   |_|\___|_| |_| |_| .__/ \___/  |_|  |_|\__,_| .__/  
                    |_|                        |_|  
   ____                                 _                 
  / ___|___  _ __ ___  _ __   __ _ _ __(_)___  ___  _ __  
 | |   / _ \| '_ ` _ \| '_ \ / _` | '__| / __|/ _ \| '_ \ 
 | |__| (_) | | | | | | |_) | (_| | |  | \__ \ (_) | | | |
  \____\___/|_| |_| |_| .__/ \__,_|_|  |_|___/\___/|_| |_|
                      |_|                   
*/

#include <Bela.h>
#include <WriteFile.h>
#include <Scope.h>


///%%%%% OSC VARIABLES %%%%%%%%%%%%%%%%%

OSCClient oscClient;

///%%%%% AUDIO RECORDING VARIABLES %%%%%%%%%%%%%%%%%
WriteFile audioFile;
int buffersize;
float* audioBuffer;
int audioCounter = 0;

///%%%%% SENSOR LOGGING VARIABLES %%%%%%%%%%%%%%%%%
WriteFile ankle_Sensor;
WriteFile piezo_Sensor;

float seconds = 0.f;
float input = 0.f;
int windowSize;
int audioFramesPerAnalog;
int measuredSamps = 0;
float bpmIncrement = 0.2;
bool printNow = true;
bool posIncrementFlag = true;
bool isRunning = true;

float RAnkleY;
float KneeZ;
float piezo = 0.f;
float pot;


// %%%%%% END SENSOR LOGGIN VAIRABLES %%%%%%%%%%%%%%
// %%%%%% BLEEP VARIABLES %%%%%%%%%%%%%%%%%%%%%%%%%%

float gFrequency = 800.0;
float gPhase;
float gInverseSampleRate;
float audioOut;
float sine;
bool bleepGate = false;
int bleepCount = 0;
int innerCount = 0;
int bpmToMsToSamps = 0;
float bpm;
void updateBpm(float newBpm); // Function declaration.
int noteCount = 0;
int barCount = 0;
int bleepLengthInSamps;

// %%%%%% END BLEEP VARIABLES %%%%%%%%%%%%%%%%%%%%%%%%%%

Scope scope;

bool setup(BelaContext *context, void *userData)
{
	// For this example we need the same amount of audio and analog input and output channels
	if(context->audioInChannels != context->audioOutChannels ||
			context->analogInChannels != context-> analogOutChannels)
	{
		printf("Error: for this project, you need the same number of input and output channels.\n");
		return false;
	}
	
	gInverseSampleRate = 1.0 / context->audioSampleRate;
	gPhase = 0.0;
	audioFramesPerAnalog = context->audioFrames / context->analogFrames;
	windowSize = context->audioFrames;
	bpm = 120;
	bpmToMsToSamps = (60000 / bpm) * 44.1; // this equation determines how many samples to wait between bleeps. (44.1 is the num of samples per ms at this audioSampleRate)

	
	// -----------------------------------
	// *******WRITEFILE SETUP ************
	
	piezo_Sensor.init("Piezo(Comparison_Study).txt");
	piezo_Sensor.setFormat("%.4f'%.4f\n"); // Feature vector of size 2 (First being time).
	piezo_Sensor.setFileType(kText);
	piezo_Sensor.setHeader("");
	piezo_Sensor.setFooter("");
	
	ankle_Sensor.init("Ankle_Sensor[yAxis](Comparison_Study).txt");
	ankle_Sensor.setFormat("%.4f'%.4f\n"); // Feature vector of size 2 (First being time).
	ankle_Sensor.setFileType(kText);
	ankle_Sensor.setHeader("");
	ankle_Sensor.setFooter("");
	
	//************************************
	// *************AUDIO SETUP **********
	audioFile.init("Comparison_Audio");
	audioFile.setFormat("%f");
	audioFile.setFileType(kBinary);
	audioFile.setHeader("");
	audioFile.setFooter("");
	
	
	buffersize = 512;
	
	audioBuffer = (float *)malloc(buffersize * context->audioInChannels * sizeof(float));
	// // *************AUDIO SETUP **********

	// scope.setup(3, context->audioSampleRate);
	oscClient.setup(7563,"192.168.7.1");
	
	// This tells PD to start recording data from the NGIMU.
	oscClient.queueMessage(oscClient.newMessage.to("/start").add(1.0f).end());

	return true;
}

void render(BelaContext *context, void *userData)
{
	for(unsigned int n = 0; n < context->audioFrames; n++)
	{
		measuredSamps ++;
		seconds = context->audioFramesElapsed / context->audioSampleRate;
		
		// Accelerometer access.
			// ANALOG READING.
			if(!(n % audioFramesPerAnalog)) 
		{
			
			RAnkleY = analogRead(context, n/audioFramesPerAnalog, 1);
			piezo = analogRead(context, n/audioFramesPerAnalog, 7);
		}
		
		// ************ DATA LOGGING *******************
		if(measuredSamps == 44)
		{
			measuredSamps = 0;
			piezo_Sensor.log(seconds);
			piezo_Sensor.log(piezo);
		
			ankle_Sensor.log(seconds);
			ankle_Sensor.log(RAnkleY);

			// LOG THEM HERE!!!

		}
		// *********************************************
		// scope.log(accels[0][0], accels[2][0]);
		
		//~~~~~~~~~~~~~~~~~~ AUDIO SECTION ~~~~~~~~~~~~~~~~~~
		
		sine = 0.1 * sinf(gPhase);
		gPhase += 2.0 * M_PI * gFrequency * gInverseSampleRate;
		if(gPhase > 2.0 * M_PI)
			gPhase -= 2.0 * M_PI;
		
		for(unsigned int ch = 0; ch < context->audioInChannels; ch++)
		{	
			// Writing incoming audio to a buffer.
			float in = audioRead(context, n, ch);
			audioBuffer[audioCounter + ch] = in + audioOut;
			
			if (bleepGate == true)
			{
			audioWrite(context, n, ch, audioOut);
			audioOut = sine;
			innerCount ++;
				if (innerCount >= (4000))
				{
					bleepGate = false;
					innerCount = 0;
				}
			}
			
			else if (bleepGate == false)
			{
				audioWrite(context, n, ch, 0);
				audioOut = 0;
			}
		}
		
		audioCounter++;
		if(audioCounter == buffersize)
		{
			audioCounter = 0;
			audioFile.log(audioBuffer, buffersize);
		}
		
		bleepCount ++;
        if (bleepCount  == (int)bpmToMsToSamps)
        {
        	if(isRunning)
        	{
            bleepGate = true;
        	}
            noteCount++;
            if(!(noteCount % 4))
            {
            	barCount ++;
            }
            
            if(posIncrementFlag)
            {
            bpm = bpm + bpmIncrement; // increment the bpm slowly.
            }
            else // If bool is switched then we want to decrease in tempo.
            {
            bpm = bpm - bpmIncrement;	
            }
            bpmToMsToSamps = (60000 / bpm) * 44.1; // recalculate the samples between bleeps.
            rt_printf("%.3f    barCount = : %d", bpm, barCount);
            
            bleepCount = 0;
            
            // @@@@@@@@@== TEMPO MAP SECTION==@@@@@@@@@@@@@@@@@@
            // After certain bar "checkpoints" are met, the Bpm increment changes to create a tempo map.
            
            if(barCount == 10)
            {
            	bpmIncrement = 0.6;
            	posIncrementFlag = false;
            }
            
            else if(barCount == 22)
            {
            	bpmIncrement = 0.5;
           		posIncrementFlag = true;
            }
            
            else if(barCount == 26)
            {
            	bpmIncrement = 2.0;
            }
            
            else if(barCount == 34)
            {
            	bpmIncrement = 0.5;
            	posIncrementFlag = false;

            }
            
            else if(barCount == 56)
            {
            	bpmIncrement = 0;	
            	posIncrementFlag = true;
            }
            else if (barCount == 68)
            {
            	bpmIncrement = 1.0;
            	posIncrementFlag = false;
            }
            
            else if (barCount == 80)
            {
            	bpmIncrement = 0.0;
            	isRunning = false;
            	rt_printf("FINISHED!!!\n");
            	
            }
           
        }
		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	}
}

void cleanup(BelaContext *context, void *userData)
{
	// This tells PD to stop recording data from the NGIMU.
	oscClient.sendMessageNow(oscClient.newMessage.to("/start").add(2.0f).end());
}




