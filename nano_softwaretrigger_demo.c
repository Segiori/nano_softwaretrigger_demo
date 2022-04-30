
#include "stdio.h"
#include "cordef.h"
#include "gevapi.h"				//!< GEV lib definitions.
#include "SapX11Util.h"
#include "X_Display_utils.h"
#include <sched.h>


#define MAX_NETIF					8
#define MAX_CAMERAS_PER_NETIF	32
#define MAX_CAMERAS		(MAX_NETIF * MAX_CAMERAS_PER_NETIF)

// Enable/disable Bayer to RGB conversion
// (If disabled - Bayer format will be treated as Monochrome).
#define ENABLE_BAYER_CONVERSION 1

// Enable/disable buffer FULL/EMPTY handling (cycling)
#define USE_SYNCHRONOUS_BUFFER_CYCLING	0

// Enable/disable transfer tuning (buffering, timeouts, thread affinity).
#define TUNE_STREAMING_THREADS 0

#define NUM_BUF	8
void *m_latestBuffer = NULL;

typedef struct tagMY_CONTEXT
{
   X_VIEW_HANDLE     View;
	GEV_CAMERA_HANDLE camHandle;
	int					depth;
	int 					format;
	void 					*convertBuffer;
	BOOL					convertFormat;
	BOOL              exit;
}MY_CONTEXT, *PMY_CONTEXT;

char GetKey()
{
   char key = getchar();
   while ((key == '\r') || (key == '\n'))
   {
      key = getchar();
   }
   return key;
}

void PrintMenu()
{
   printf("GRAB CTL : [S]=stop, [1-9]=snap N, [G]=continuous, [A]=Abort\n");
   printf("MISC     : [Q]or[ESC]=end \n");
}

void * ImageDisplayThread( void *context)
{
	MY_CONTEXT *displayContext = (MY_CONTEXT *)context;

	if (displayContext != NULL)
	{
		// While we are still running.
		while(!displayContext->exit)
		{
			GEV_BUFFER_OBJECT *img = NULL;
			GEV_STATUS status = 0;
	
			// Wait for images to be received
			status = GevWaitForNextImage(displayContext->camHandle, &img, 1000);
                        status = GevSetFeatureValueAsString(displayContext->camHandle, "TriggerSoftware", "On");

			if ((img != NULL) && (status == GEVLIB_OK))
			{
				if (img->status == 0)
				{
					m_latestBuffer = img->address;
					// Can the acquired buffer be displayed?
					if ( IsGevPixelTypeX11Displayable(img->format) || displayContext->convertFormat )
					{
						// Convert the image format if required.
						if (displayContext->convertFormat)
						{
							int gev_depth = GevGetPixelDepthInBits(img->format);
							// Convert the image to a displayable format.
							//(Note : Not all formats can be displayed properly at this time (planar, YUV*, 10/12 bit packed).
							ConvertGevImageToX11Format( img->w, img->h, gev_depth, img->format, img->address, \
													displayContext->depth, displayContext->format, displayContext->convertBuffer);
					
							// Display the image in the (supported) converted format. 
							Display_Image( displayContext->View, displayContext->depth, img->w, img->h, displayContext->convertBuffer );				
						}
						else
						{
							// Display the image in the (supported) received format. 
							Display_Image( displayContext->View, img->d,  img->w, img->h, img->address );
						}
					}
					else
					{
					}
				}
				else
				{
					// Image had an error (incomplete (timeout/overflow/lost)).
					// Do any handling of this condition necessary.
				}
			}
#if USE_SYNCHRONOUS_BUFFER_CYCLING
			if (img != NULL)
			{
				// Release the buffer back to the image transfer process.
				GevReleaseImage( displayContext->camHandle, img);
			}
#endif
		}
	}
	pthread_exit(0);	
}

int main(int argc, char* argv[])
{
	GEV_DEVICE_INTERFACE  pCamera[MAX_CAMERAS] = {0};
	GEV_STATUS status;
	int numCamera = 0;
	int camIndex = 0;
   X_VIEW_HANDLE  View = NULL;
	MY_CONTEXT context = {0};
   pthread_t  tid;
	char c;
	int done = FALSE;

	// Boost application RT response (not too high since GEV library boosts data receive thread to max allowed)
	// SCHED_FIFO can cause many unintentional side effects.
	// SCHED_RR has fewer side effects.
	// SCHED_OTHER (normal default scheduler) is not too bad afer all.
	if (0)
	{
		//int policy = SCHED_FIFO;
		int policy = SCHED_RR;
		pthread_attr_t attrib;
		int inherit_sched = 0;
		struct sched_param param = {0};

		// Set an average RT priority (increase/decrease to tuner performance).
		param.sched_priority = (sched_get_priority_max(policy) - sched_get_priority_min(policy)) / 2;
		
		// Set scheduler policy
		pthread_setschedparam( pthread_self(), policy, &param); // Don't care if it fails since we can't do anyting about it.
		
		// Make sure all subsequent threads use the same policy.
		pthread_attr_init(&attrib);
		pthread_attr_getinheritsched( &attrib, &inherit_sched);
		if (inherit_sched != PTHREAD_INHERIT_SCHED)
		{
			inherit_sched = PTHREAD_INHERIT_SCHED;
			pthread_attr_setinheritsched(&attrib, inherit_sched);
		}
	}


	//============================================================================
	// Greetings
	printf ("\nGigE Vision Library : Genie Nano Trigger via ACTION_CMD Example Program (%s)\n", __DATE__);
	printf ("Copyright (c) 2018, Teledyne DALSA.\nAll rights reserved.\n\n");

	//====================================================================================
	// DISCOVER Cameras
	//
	// Get all the IP addresses of attached network cards.

	status = GevGetCameraList( pCamera, MAX_CAMERAS, &numCamera);

	printf ("%d camera(s) on the network\n", numCamera);

	// Select the first camera found (unless the command line has a parameter = the camera index)
	if (numCamera != 0)
	{
		if (argc > 1)
		{
			sscanf(argv[1], "%d", &camIndex);
			if (camIndex >= (int)numCamera)
			{
				printf("Camera index out of range - only %d camera(s) are present\n", numCamera);
				camIndex = -1;
			}
		}

		if (camIndex != -1)
		{
			//====================================================================
			// Connect to Camera
			//
			// Direct instantiation of GenICam XML-based feature node map.
			int i;
			int type;
			UINT32 height = 0;
			UINT32 width = 0;
			UINT32 format = 0;
			UINT32 maxHeight = 1024;
			UINT32 maxWidth = 2048;
			UINT32 maxDepth = 2;
			UINT64 size;
			UINT64 payload_size;
			int numBuffers = NUM_BUF;
			PUINT8 bufAddress[NUM_BUF];
			GEV_CAMERA_HANDLE handle = NULL;
			UINT32 pixFormat = 0;
			UINT32 pixDepth = 0;
			UINT32 convertedGevFormat = 0;
			
			//====================================================================
			// Open the camera.
			status = GevOpenCamera( &pCamera[camIndex], GevControlMode, &handle);
			if (status == 0)
			{
				GEV_CAMERA_OPTIONS camOptions = {0};

				// Adjust the camera interface options if desired (see the manual)
				GevGetCameraInterfaceOptions( handle, &camOptions);
				//camOptions.heartbeat_timeout_ms = 60000;		// For debugging (delay camera timeout while in debugger)
				camOptions.heartbeat_timeout_ms = 5000;		// Disconnect detection (5 seconds)

#if TUNE_STREAMING_THREADS
				// Some tuning can be done here. (see the manual)
				camOptions.streamFrame_timeout_ms = 1001;				// Internal timeout for frame reception.
				camOptions.streamNumFramesBuffered = 4;				// Buffer frames internally.
				camOptions.streamMemoryLimitMax = 64*1024*1024;		// Adjust packet memory buffering limit.	
				camOptions.streamPktSize = 9180;							// Adjust the GVSP packet size.
				camOptions.streamPktDelay = 10;							// Add usecs between packets to pace arrival at NIC.
				
				// Assign specific CPUs to threads (affinity) - if required for better performance.
				{
					int numCpus = _GetNumCpus();
					if (numCpus > 1)
					{
						camOptions.streamThreadAffinity = numCpus-1;
						camOptions.serverThreadAffinity = numCpus-2;
					}
				}
#endif
				// Write the adjusted interface options back.
				GevSetCameraInterfaceOptions( handle, &camOptions);

				// Set up the camera (assumed to be Genie Nano) to trigger 
				// frames based on Action1 being asserted via ACTION_CMD.
				//
				// Note : Change the image size here if desired.
				//
				
				//GevSetFeatureValueAsString(handle, "Height", "512");
				//GevSetFeatureValueAsString(handle, "Width", "512");
				
				// Change the default values for the DeviceKey, GroupKey, and GroupMask.
				//
				// Nano Defaults are : DeviceKey = 1, GroupKey = 0, GroupMask = 3 (for Action1 | Action2)
				//
				// (The source of the ACTION_CMD message must know these vaues 
				// - leave as default unless more control is needed for multiple devices).
				
				//status = GevSetFeatureValueAsString(handle, "ActionSelector", "1");
				//status = GevSetFeatureValueAsString(handle, "ActionDeviceKey", "1");
				//status = GevSetFeatureValueAsString(handle, "ActionGroupKey", "0");
				//status = GevSetFeatureValueAsString(handle, "ActionGroupMask", "1");
				
				// Set the Nano to Trigger on Action1.
				status = GevSetFeatureValueAsString(handle, "TriggerMode", "On");
				status = GevSetFeatureValueAsString(handle, "TriggerSelector", "FrameStart");
                                status = GevSetFeatureValueAsString(handle, "TriggerSoftware", "On");
				status = GevSetFeatureValueAsString(handle, "TriggerSource", "Software");


				// Get the camera width, height, and pixel format
				GevGetFeatureValue(handle, "Width", &type, sizeof(width), &width);
				GevGetFeatureValue(handle, "Height", &type, sizeof(height), &height);
				GevGetFeatureValue(handle, "PixelFormat", &type, sizeof(format), &format);

				// Access some features using the C-compatible functions.
				{
					UINT32 val = 0;
					char value_str[MAX_PATH] = {0};
						
					printf("Camera ROI set for \n\t");
					GevGetFeatureValueAsString( handle, "Height", &type, MAX_PATH, value_str);
					printf("Height = %s\n\t", value_str);
					GevGetFeatureValueAsString( handle, "Width", &type, MAX_PATH, value_str);
					printf("Width = %s\n\t", value_str);
					GevGetFeatureValueAsString( handle, "PixelFormat", &type, MAX_PATH, value_str);
					printf("PixelFormat (str) = %s\n\t", value_str);

					GevGetFeatureValue(handle, "PixelFormat", &type, sizeof(UINT32), &val);
					printf("PixelFormat (val) = 0x%x\n", val);
				}



				if (status == 0)
				{
					//=================================================================
					// Set up a grab/transfer from this camera
					//
					GevGetPayloadParameters( handle,  &payload_size, (UINT32 *)&type);
					maxHeight = height;
					maxWidth = width;
					maxDepth = GetPixelSizeInBytes(GevGetUnpackedPixelType(format));

					// Allocate image buffers (adjusting for any unpacking of packed pixels)
					// (Either the image size or the payload_size, whichever is larger).
					size = maxDepth * maxWidth * maxHeight;
					size = (payload_size > size) ? payload_size : size;
					for (i = 0; i < numBuffers; i++)
					{
						bufAddress[i] = (PUINT8)malloc(size);
						memset(bufAddress[i], 0, size);
					}
					

#if USE_SYNCHRONOUS_BUFFER_CYCLING
					// Initialize a transfer with synchronous buffer handling.
					status = GevInitializeTransfer( handle, SynchronousNextEmpty, size, numBuffers, bufAddress);
#else
					// Initialize a transfer with asynchronous buffer handling.
					status = GevInitializeTransfer( handle, Asynchronous, size, numBuffers, bufAddress);
#endif

					// Create an image display window.
					// This works best for monochrome and RGB. The packed color formats (with Y, U, V, etc..) require 
					// conversion as do, if desired, Bayer formats.
					// (Packed pixels are unpacked internally unless passthru mode is enabled).

					// Translate the raw pixel format to one suitable for the (limited) Linux display routines.			

					status = GetX11DisplayablePixelFormat( ENABLE_BAYER_CONVERSION, format, &convertedGevFormat, &pixFormat);

					if (format != convertedGevFormat) 
					{
						// We MAY need to convert the data on the fly to display it.
						if (GevIsPixelTypeRGB(convertedGevFormat))
						{
							// Conversion to RGB888 required.
							pixDepth = 32;	// Assume 4 8bit components for color display (RGBA)
							context.format = Convert_SaperaFormat_To_X11( pixFormat);
							context.depth = pixDepth;
							context.convertBuffer = malloc((maxWidth * maxHeight * ((pixDepth + 7)/8)));
							context.convertFormat = TRUE;
						}
						else
						{
							// Converted format is MONO - generally this is handled
							// internally (unpacking etc...) unless in passthru mode.
							// (						
							pixDepth = GevGetPixelDepthInBits(convertedGevFormat);
							context.format = Convert_SaperaFormat_To_X11( pixFormat);
							context.depth = pixDepth;							
							context.convertBuffer = NULL;
							context.convertFormat = FALSE;
						}
					}
					else
					{
						pixDepth = GevGetPixelDepthInBits(convertedGevFormat);
						context.format = Convert_SaperaFormat_To_X11( pixFormat);
						context.depth = pixDepth;
						context.convertBuffer = NULL;
						context.convertFormat = FALSE;
					}
					
					View = CreateDisplayWindow("GigE-V GenApi Console Demo", TRUE, height, width, pixDepth, pixFormat, FALSE ); 

					// Create a thread to receive images from the API and display them.
					context.View = View;
					context.camHandle = handle;
					context.exit = FALSE;
		   		pthread_create(&tid, NULL, ImageDisplayThread, &context); 

					
		         // Call the main command loop or the example.
		         PrintMenu();
		         while(!done)
		         {
		            c = GetKey();
	            
		            // Stop
		            if ((c == 'S') || (c=='s') || (c == '0'))
		            {
							GevStopTransfer(handle);
		            }
		            //Abort
		            if ((c == 'A') || (c=='a'))
		            {
	 						GevAbortTransfer(handle);
						}
		            // Snap N (1 to 9 frames)
		            if ((c >= '1')&&(c<='9'))
		            {
							for (i = 0; i < numBuffers; i++)
							{
								memset(bufAddress[i], 0, size);
							}

							status = GevStartTransfer( handle, (UINT32)(c-'0'));
							if (status != 0) printf("Error starting grab - 0x%x  or %d\n", status, status); 
						}
		            // Continuous grab.
		            if ((c == 'G') || (c=='g'))
		            {
							for (i = 0; i < numBuffers; i++)
							{
								memset(bufAddress[i], 0, size);
							}
	 						status = GevStartTransfer( handle, -1);
							if (status != 0) printf("Error starting grab - 0x%x  or %d\n", status, status); 
		            }
					
		            if (c == '?')
		            {
		               PrintMenu();
		            }

		            if ((c == 0x1b) || (c == 'q') || (c == 'Q'))
		            {
							GevStopTransfer(handle);
		               done = TRUE;
							context.exit = TRUE;
		   				pthread_join( tid, NULL);      
		            }
		         }

					// Disable the trigger before exitting.
					GevSetFeatureValueAsString(handle, "TriggerMode", "Off");
					
					GevAbortTransfer(handle);
					status = GevFreeTransfer(handle);
					DestroyDisplayWindow(View);


					for (i = 0; i < numBuffers; i++)
					{	
						free(bufAddress[i]);
					}
					if (context.convertBuffer != NULL)
					{
						free(context.convertBuffer);
						context.convertBuffer = NULL;
					}
				}
				GevCloseCamera(&handle);
			}
			else
			{
				printf("Error : 0x%0x : opening camera\n", status);
			}
		}
	}

	// Close down the API.
	GevApiUninitialize();

	// Close socket API
	_CloseSocketAPI ();	// must close API even on error


	//printf("Hit any key to exit\n");
	//kbhit();

	return 0;
}
