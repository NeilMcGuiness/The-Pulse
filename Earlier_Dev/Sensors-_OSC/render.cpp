#include <Bela.h>
#include <OSCServer.h>
#include <OSCClient.h>
#include <Scope.h>

OSCServer oscServer;
OSCClient oscClient;
OSCClient oscClient2;

int gAudioFramesPerAnalogFrame;
int count;
int count2;
int windowSize = 1000;
int tiltWindow = 2205;
Scope scope;
float lAnkle;
float rAnkle;
float knee;
float lWristx;
float lWristz;


float rWristx;
float rWristz;


float piezo;

float accelSlope;
float accelOffset;

float xGs;
float yGs;
float zGs;
float magnitude;
float pitch;
float roll;

bool setup(BelaContext *context, void *userData)
{
	 // setup the OSC server to receive on port 7562
    oscServer.setup(7562);
    // setup the OSC client to send on port 7563
    oscClient.setup(7564,"192.168.7.1");
    oscClient2.setup(7565,"192.168.7.1");

    
    	// Check if analog channels are enabled
	if(context->analogFrames == 0 || context->analogFrames > context->audioFrames) {
		rt_printf("Error: this example needs analog enabled, with 4 or 8 channels\n");
		return false;
	}

	// Check that we have the same number of inputs and outputs.
	if(context->audioInChannels != context->audioOutChannels ||
			context->analogInChannels != context-> analogOutChannels){
		printf("Error: for this project, you need the same number of input and output channels.\n");
		return false;
	}

	// Useful calculations
	gAudioFramesPerAnalogFrame = context->audioFrames / context->analogFrames;
	
	scope.setup(1, context->audioSampleRate);
	
	accelSlope = 2.0 / 0.175;
	accelOffset = -1 - (0.32 * accelSlope);

    
	return true;
}

void render(BelaContext *context, void *userData)
{
	
for(unsigned int n = 0; n < context->audioFrames; n++)
{
	if(!(n % gAudioFramesPerAnalogFrame)) 
	{		
		// On even audio samples: read analog inputs and update frequency and amplitude
		  
		  lAnkle = analogRead(context, n/gAudioFramesPerAnalogFrame, 0);
		  rAnkle = analogRead(context, n/gAudioFramesPerAnalogFrame, 1);
		  knee = analogRead(context, n/gAudioFramesPerAnalogFrame, 2);
		  lWristx = analogRead(context, n/gAudioFramesPerAnalogFrame, 3);
		  lWristz = analogRead(context, n/gAudioFramesPerAnalogFrame, 4);
		  rWristx = analogRead(context, n/gAudioFramesPerAnalogFrame, 5);
		  rWristz = analogRead(context, n/gAudioFramesPerAnalogFrame, 6);
		  piezo = analogRead(context, n/gAudioFramesPerAnalogFrame, 7);

	}
	
	count++;
	count2 ++;
	
	if(count >= windowSize)
	{
		// send OSC message
		// oscClient.queueMessage(oscClient.newMessage.to("/inputs/analogue").add(lAnkle).add(rAnkle).add(knee).add(lWristx).add(lWristz).add(rWristx).add(rWristz).add(piezo).end());
		// oscClient2.queueMessage(oscClient.newMessage.to("/wek/inputs").add(roll).add(pitch).end());
		oscClient.queueMessage(oscClient.newMessage.to("/inputs/analogue").add(piezo).add(rAnkle).end());

		count = 0;
	}
	
	if (count2 >= tiltWindow)
	{
		// Calculations
		
  //  	xGs = (accelX * accelSlope) + accelOffset;
		// yGs = (accelY * accelSlope) + accelOffset;
		// zGs = (accelZ * accelSlope) + accelOffset;
		

		magnitude = sqrt((xGs * xGs) + (yGs + yGs) + (zGs + zGs));
		roll = atan2(xGs, zGs);
		
		float sqYZ = sqrt((yGs * yGs) + (zGs * zGs));
		pitch = atan2(xGs, sqYZ);
			  
		rt_printf("Y = %.3f	 %.3f\n", lAnkle, rAnkle);
		
		count2 = 0;
	}
	
	scope.log (pitch);
	}
}

void cleanup(BelaContext *context, void *userData)
{

}