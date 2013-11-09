//***********************************************************
//* mixer.c
//***********************************************************

//***********************************************************
//* Includes
//***********************************************************

#include <string.h>
#include <avr/io.h>
#include <stdbool.h>
#include <util/delay.h>
#include "..\inc\io_cfg.h"
#include "..\inc\rc.h"
#include "..\inc\isr.h"
#include "..\inc\servos.h"
#include "..\inc\pid.h"
#include "..\inc\main.h"
#include <avr/pgmspace.h> 
#include "..\inc\mixer.h"

//************************************************************
// Prototypes
//************************************************************

void ProcessMixer(void);
void UpdateServos(void);
void UpdateLimits(void);
void get_preset_mix (const channel_t*);
int16_t scale32(int16_t value16, int16_t multiplier16);
int16_t scale_percent(int8_t value);
int16_t scale_percent_nooffset(int8_t value);

//************************************************************
// Mix tables (both RC inputs and servo/ESC outputs)
//************************************************************

// Aeroplane mixer defaults
const channel_t AEROPLANE_MIX[MAX_OUTPUTS] PROGMEM = 
{
	// Rudder -= Yaw; (normal)
	// Aileron -= Roll; (normal)
	// Elevator += Pitch; (normal)

	// Value, 
	// source, source_vol, roll_gyro,pitch_gyro,yaw_gyro,roll_acc,pitch_acc
	// offset, source_b,source_b_volume,source_c,source_c_volume,source_d,source_d_volume

	{0,THROTTLE,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut1 (Throttle)
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut2
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut3
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut4
	{0,ELEVATOR,100,OFF,ON,OFF,OFF,ON,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut5 (Elevator)
	{0,AILERON,100,ON,OFF,OFF,ON,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut6 (Left aileron)
	{0,NOCHAN,100,ON,OFF,OFF,ON,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},			// ServoOut7 (Right aileron)
	{0,RUDDER,100,OFF,OFF,ON,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut8 (Rudder)

}; 

const channel_t FLYING_WING_MIX[MAX_OUTPUTS] PROGMEM = 
{
	// Rudder -= Yaw (normal)
	// L.Elevon + Roll (reversed) - Pitch (normal)
	// R.Elevon + Roll (reversed) + Pitch (reversed)

	// Value, 
	// source, source_vol,roll_gyro,pitch_gyro,yaw_gyro,roll_acc,pitch_acc
	// offset, source_b,source_b_volume,source_c,source_c_volume,source_d,source_d_volume

	{0,THROTTLE,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut1 (Throttle)
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut2
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut3
	{0,AILERON,100,ON,OFF,OFF,ON,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut4 (Raw stabilised aileron)
	{0,ELEVATOR,100,OFF,ON,OFF,OFF,ON,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut5 (Raw stabilised elevator)
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,OUT4,50,OUT5,50,NOMIX,0},		// ServoOut6 (Left elevon)
	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,OUT4,-50,OUT5,50,NOMIX,0},		// ServoOut7 (Right elevon)
	{0,RUDDER,100,OFF,OFF,ON,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut8 (Rudder)
}; 

const channel_t CAM_STAB[MAX_OUTPUTS] PROGMEM = 
{
 	// For non-controlled, use
	// M2 Pitch (Tilt) + Pitch gyro;
 	// M3 Yaw	(Pan) + Yaw;
 	// M4 Roll (Roll - only for 3-axis gimbals) + Roll gyro;

 	// For controlled axis, use
	// M6 Pitch (Tilt) + Pitch gyro;
 	// M7 Yaw	(Pan) + Yaw;
 	// M8 Roll (Roll - only for 3-axis gimbals) + Roll gyro;

	{0,NOCHAN,100,OFF,OFF,OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut1 (Throttle)
	{0,NOCHAN,100,OFF,ON, OFF,OFF,ON, 0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut2 (Pitch axis)
	{0,NOCHAN,100,OFF,OFF,ON, OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut3 (Yaw axis)
	{0,NOCHAN,100,ON,OFF,OFF,ON, OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut4 (Roll axis)
	{0,NOCHAN,100,OFF,OFF, OFF,OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut5 
	{0,ELEVATOR,100,OFF,ON,OFF,OFF,ON, 0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut6 (Pitch axis)
	{0,RUDDER,100,OFF,OFF,ON, OFF,OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut7 (Yaw axis)
	{0,AILERON,100,ON,OFF,OFF,ON, OFF,0,NOMIX,0,NOMIX,0,NOMIX,0},		// ServoOut8 (Roll axis)

};

//************************************************************

void ProcessMixer(void)
{
	static	int16_t flap = 0;
	static	int16_t slowFlaps = 0;
	static	uint8_t flapskip;
	static	int16_t roll = 0;
	static	int16_t	old_z = 0;

	uint8_t i, outputs;
	int8_t	speed;
	int16_t temp1 = 0;
	int16_t temp2 = 0;
	int16_t	temp3 = 0;
	bool	TwoAilerons = false;
	bool	FlapLock = false;

	//************************************************************
	// Limit output mixing as needed to save processing power
	//************************************************************

	if (Config.CamStab == ON)
	{
		outputs = MIN_OUTPUTS;		// 4 Channels
	}
	else
	{
		outputs = PSUEDO_OUTPUTS;	// 12 Channels
	}

	//************************************************************
	// Zero all channel values to start
	//************************************************************

	for (i = 0; i < outputs; i++)
	{
		Config.Channel[i].value = 0;
	}

	//************************************************************
	// Un-mix flaps from flaperons as required
	//************************************************************ 

	if (Config.FlapChan != NOCHAN)
	{
		// Update flap only if ailerons are within measureable positions
		if ((RCinputs[AILERON] > -1200) && 
			(RCinputs[AILERON] < 1200) &&
			(RCinputs[Config.FlapChan] > -1200) && 
			(RCinputs[Config.FlapChan] < 1200))
		{
			flap = RCinputs[AILERON] - RCinputs[Config.FlapChan]; 	
			flap = flap >> 1; 	
			FlapLock = false;
		}
		else
		{
			FlapLock = true;
		}
	}
	else
	{
		flap = 0;
	}

	//************************************************************
	// Un-mix ailerons from flaperons as required in all modes
	//************************************************************

	// If in AEROPLANE mixer mode and flaperons set up
	if ((Config.FlapChan != NOCHAN) && (Config.MixMode == AEROPLANE))
	{
		// Remove flap signal from flaperons, leaving ailerons only
		roll = RCinputs[AILERON] + RCinputs[Config.FlapChan];

		// Otherwise throw is 50% of both signals
		RCinputs[AILERON] = roll >> 1;
		
		// Copy to second aileron channel
		RCinputs[Config.FlapChan] = RCinputs[AILERON];
	}

	//************************************************************
	// Process RC mixing and source volume calculation
	//************************************************************

	for (i = 0; i < outputs; i++)
	{
		// Get requested volume of Source A
		temp1 = RCinputs[Config.Channel[i].source_a];
		temp1 = scale32(temp1, Config.Channel[i].source_a_volume);

		// Save solution for now
		Config.Channel[i].value = temp1;
	}

	//************************************************************
	// Add in some height damping if required
	//************************************************************

	if (Config.Dampen != 0)
	{
		// Work change in Z value
		temp1 = old_z - PID_ACCs[YAW];
		temp1 *= Config.Dampen;

		if (temp1 > MAX_ZGAIN)
		{
			temp1 = MAX_ZGAIN;
		}

		// Add dampening value to throttle values
		for (i = 0; i < outputs; i++)
		{
			if (Config.Channel[i].source_a == THROTTLE)
			{
				Config.Channel[i].value += temp1;
			}
		}

		// Update old acc value
		old_z = PID_ACCs[YAW];
	}

	//************************************************************
	// Mix in gyros
	//************************************************************ 

	// Use PID gyro values
	if (Flight_flags & (1 << Stability))
	{
		for (i = 0; i < outputs; i++)
		{
			// Get current channel value
			temp1 = Config.Channel[i].value;

			// Mix in gyros
			switch (Config.Channel[i].roll_gyro)
			{
				case ON:
					temp1 = temp1 - PID_Gyros[ROLL];
					break;
				case REV:
					temp1 = temp1 + PID_Gyros[ROLL];
					break;	
				default:
					break;
			}
			switch (Config.Channel[i].pitch_gyro)
			{
				case ON:
					temp1 = temp1 + PID_Gyros[PITCH];
					break;
				case REV:
					temp1 = temp1 - PID_Gyros[PITCH];
					break;	
				default:
					break;
			}
			switch (Config.Channel[i].yaw_gyro)
			{
				case ON:
					temp1 = temp1 - PID_Gyros[YAW];
					break;
				case REV:
					temp1 = temp1 + PID_Gyros[YAW];
					break;	
				default:
					break;
			}

			// Save solution for now
			Config.Channel[i].value = temp1;
		}
	} // Stability

	//************************************************************
	// Mix in accelerometers
	//************************************************************ 

	// Add PID acc values including trim
	if (Flight_flags & (1 << AutoLevel))
	{
		temp1 = 0;
		temp2 = 0;

		// Offset Autolevel trims in failsafe mode
		if ((Config.FailsafeType == 1) && (Flight_flags & (1 << Failsafe)) && (Config.CamStab == OFF))
		{
			temp1 = Config.FailsafeAileron;
			temp1 = temp1 << 2;
			temp2 = Config.FailsafeElevator;
			temp2 = temp2 << 2;
		}

		// Add autolevel trims * 4		
		temp1 += (Config.FlightMode[Config.Flight].AccRollZeroTrim << 2); // Roll trim
		temp2 += (Config.FlightMode[Config.Flight].AccPitchZeroTrim << 2);// Pitch trim

		// Mix in accelerometers
		for (i = 0; i < outputs; i++)
		{
			// Get solution
			temp3 = Config.Channel[i].value;

			switch (Config.Channel[i].roll_acc)
			{
				case ON:
					// Add in Roll trim
					temp3 += temp1;
					temp3 = temp3 - PID_ACCs[ROLL];
					break;
				case REV:
					// Add in Roll trim
					temp3 -= temp1;
					temp3 = temp3 + PID_ACCs[ROLL];
					break;	
				default:
					break;
			}

			switch (Config.Channel[i].pitch_acc)
			{
				case ON:
					// Add in Pitch trim
					temp3 += temp2;
					temp3 = temp3 + PID_ACCs[PITCH];
					break;
				case REV:
					// Add in Pitch trim
					temp3 -= temp2;
					temp3 = temp3 - PID_ACCs[PITCH];
					break;	
				default:
					break;
			}

			// Save solution for now
			Config.Channel[i].value = temp3;
		}
	} // Autolevel

	//************************************************************
	// Process differential if set up and two ailerons used
	//************************************************************

	if ((Config.FlapChan != NOCHAN) && (Config.Differential != 0))
	{
		// Search through outputs for aileron channels
		for (i = 0; i < outputs; i++)
		{
			// Get current channel value
			temp1 = Config.Channel[i].value;

			// If some kind of aileron channel
			if ((Config.Channel[i].source_a == AILERON) || (Config.Channel[i].source_a == Config.FlapChan))
			{
				// For the second aileron (RHS)
				if (TwoAilerons)			
				{
					// Limit negative-going values
					if (temp1 < 0)
					{
						temp1 = scale32(temp1, (100 - Config.Differential));
						Config.Channel[i].value = temp1;
					}
				}

				// For the first aileron (LHS) 
				// Limit positive-going values
				else if (temp1 > 0)			
				{
					temp1 = scale32(temp1, (100 - Config.Differential));
					Config.Channel[i].value = temp1;
					TwoAilerons = true; // Found an aileron
				}

				// Else was the normal side of the first aileron
				else
				{
					TwoAilerons = true; // Found an aileron
				}
			}

		}
		// Reset after all outputs done
		TwoAilerons = false;
	}

	//************************************************************
	// Re-mix flaps from flaperons as required
	//************************************************************ 

	// The flap part of the signal has been removed so we have to reinsert it here.
	if ((Config.FlapChan != NOCHAN) && (Config.MixMode == AEROPLANE))
	{
		// If flapspeed is set to anything other than zero (normal)
		if (Config.flapspeed) 
		{
			// Do flap speed control
			if (((slowFlaps - flap) >= 1) || ((slowFlaps - flap) <= -1))	// Difference larger than one step, so ok
			{
				speed = 5;					// Need to manipulate speed as target approaches									
			}
			else
			{
				speed = 1;					// Otherwise this will oscillate
			}

			if ((slowFlaps < flap) && (flapskip == Config.flapspeed))
			{
				slowFlaps += speed;
			} 
			else if ((slowFlaps > flap) && (flapskip == Config.flapspeed)) 
			{
				slowFlaps -= speed;
			}
			
		} 
		// No speed control requested so copy flaps
		else
		{
		 	slowFlaps = flap;
		}

		flapskip++;
		if (flapskip > Config.flapspeed) flapskip = 0;

		for (i = 0; i < outputs; i++)
		{
			// Get solution
			temp1 = Config.Channel[i].value;

			// Restore flaps
			if (Config.Channel[i].source_a == AILERON)
			{
				temp1 += slowFlaps;
			}
			if (Config.Channel[i].source_a == Config.FlapChan)
			{
				temp1 -= slowFlaps;
			}

			// Update channel data solution
			Config.Channel[i].value = temp1;
		} // Flaps
	}
		
	//************************************************************
	// Process output mixers
	//************************************************************ 

	for (i = 0; i < outputs; i++)
	{
		// Get primary value
		temp1 = Config.Channel[i].value;
		
		// Mix in other outputs here
		if ((Config.Channel[i].output_b_volume !=0) && (Config.Channel[i].output_b != NOMIX)) // Mix in first extra output
		{

			// Is the source an RC input?
			if (Config.Channel[i].output_b > (PSUEDO_OUTPUTS - 1))
			{
				// Yes, calculate RC channel number from source number and return RC value
				temp2 = RCinputs[Config.Channel[i].output_b - PSUEDO_OUTPUTS];

				// Get requested volume of Source A
				temp1 = RCinputs[Config.Channel[i].source_a];
			}
			else
			{
				// No, just use the selected output
				temp2 = Config.Channel[Config.Channel[i].output_b].value;
			}

			temp2 = scale32(temp2, Config.Channel[i].output_b_volume);
			temp1 = temp1 + temp2;
		}

		if ((Config.Channel[i].output_c_volume !=0) && (Config.Channel[i].output_c != NOMIX)) // Mix in second extra source
		{
			// Is the source an RC input?
			if (Config.Channel[i].output_c > (PSUEDO_OUTPUTS - 1))
			{
				// Yes, calculate RC channel number from source number and return RC value
				temp2 = RCinputs[Config.Channel[i].output_c - PSUEDO_OUTPUTS];
			}
			else
			{
				// No, just use the selected output
				temp2 = Config.Channel[Config.Channel[i].output_c].value;
			}

			temp2 = scale32(temp2, Config.Channel[i].output_c_volume);
			temp1 = temp1 + temp2;
		}

		if ((Config.Channel[i].output_d_volume !=0) && (Config.Channel[i].output_d != NOMIX)) // Mix in third extra source
		{
			// Is the source an RC input?
			if (Config.Channel[i].output_d > (PSUEDO_OUTPUTS - 1))
			{
				// Yes, calculate RC channel number from source number and return RC value
				temp2 = RCinputs[Config.Channel[i].output_d - PSUEDO_OUTPUTS];
			}
			else
			{
				// No, just use the selected output
				temp2 = Config.Channel[Config.Channel[i].output_d].value;
			}

			temp2 = scale32(temp2, Config.Channel[i].output_d_volume);
			temp1 = temp1 + temp2;
		}

		// Reverse this channel for the eight physical outputs
		if ((i <= MAX_OUTPUTS) && (Config.Servo_reverse[i] == ON))
		{	
			temp1 = -temp1;
		}

		// Add per-channel offset
		temp1 = temp1 + Config.PerOffset[i];

		// Update channel data solution
		Config.Channel[i].value = temp1;
	}

	//************************************************************
	// Mixer transition code
	//************************************************************ 

	if (Config.MixMode == TRANSITION)
	{
		// For the overlapping channels
		for (i = OUT5; i < PSU9; i++)
		{
			// Convert number to percentage
			if (Config.TransitionSpeed == 0) 
			{
				// transition_value_16 is the RCinput / 128 so can range +/- 10
				temp3 = transition_value_16;

				// Limit extent of transition value
				if (temp3 < -8) temp3 = -8;
				if (temp3 > 8) temp3 = 8;
				temp3 += 8;

			}
			else 
			{
				temp3 = transition_counter;
			}

			// 0-16 -> 0-100
			temp3 = ((temp3 << 6) / 10); 
			if (temp3 > 100) temp3 = 100;
			if (temp3 < 0) temp3 = 0;

			// Get source channel value
			temp1 = Config.Channel[i].value;
			temp1 = scale32(temp1, (100 - temp3));

			// Get destination channel value
			temp2 = Config.Channel[i+4].value;
			temp2 = scale32(temp2, temp3);

			// Sum the mixers
			temp1 = temp1 + temp2;

			// Save transitioned solution
			Config.Channel[i].value = temp1;
		} 
	}

	//************************************************************
	// Add offset value to restore to system compatible value
	//************************************************************ 

	for (i = 0; i < MAX_OUTPUTS; i++)
	{
		Config.Channel[i].value += Config.Limits[i].trim;
	}

	//************************************************************
	// Handle Failsafe condition
	//************************************************************ 

	if ((Flight_flags & (1 << Failsafe)) && (Config.CamStab == OFF))
	{
		// Simple failsafe. Replace outputs with user-set values
		if (Config.FailsafeType == SIMPLE) 
		{
			for (i = 0; i < MAX_OUTPUTS; i++)
			{
				Config.Channel[i].value = Config.Limits[i].failsafe;
			}
		}

		// Advanced failsafe. Autolevel ON, use failsafe trims to adjust autolevel.
		if (Config.FailsafeType == ADVANCED)
		{
			for (i = 0; i < MAX_OUTPUTS; i++)
			{
				// Over-ride throttle if in CPPM mode
				if ((Config.Channel[i].source_a == THROTTLE) && (Config.RxMode == CPPM_MODE))
				{
					// Convert throttle setting to servo value
					Config.Channel[i].value = scale_percent(Config.FailsafeThrottle);				
				}

				// Tweak rudder channel						
				if (Config.Channel[i].source_a == RUDDER)
				{
					temp1 = Config.FailsafeRudder;
					temp1 = temp1 << 4;
					Config.Channel[i].value += temp1;
				}
			}
		}
	} // Failsafe
}


// Get preset mix from Program memory
void get_preset_mix(const channel_t* preset)
{
	// Clear all channels first
	memset(&Config.Channel[0].value,0,(sizeof(channel_t) * PSUEDO_OUTPUTS));
	memcpy_P(&Config.Channel[0].value,&preset[0].value,(sizeof(channel_t) * MAX_OUTPUTS));
}

// Update actual limits value with that from the mix setting percentages
// This is only done at start-up and whenever the values are changed
void UpdateLimits(void)
{
	uint8_t i;
	int8_t temp8[3] = {Config.FlightMode[Config.Flight].Roll_limit, Config.FlightMode[Config.Flight].Pitch_limit, Config.FlightMode[Config.Flight].Yaw_limit};
	int32_t temp32, gain32;
	int8_t gains[3] = {Config.FlightMode[Config.Flight].Roll.I_mult, Config.FlightMode[Config.Flight].Pitch.I_mult, Config.FlightMode[Config.Flight].Yaw.I_mult};

	// Update triggers
	Config.Autotrigger1 = scale_percent(Config.FlightMode[0].Profilelimit);
	Config.Autotrigger2 = scale_percent(Config.FlightMode[1].Profilelimit);
	Config.Autotrigger3 = scale_percent(Config.FlightMode[2].Profilelimit);
//	Config.Launchtrigger = scale_percent(Config.LaunchThrPos);

	// Update I_term limits
	for (i = 0; i < 3; i++)
	{
		temp32 	= temp8[i]; 						// Promote

		// I-term output (throw)
		// A value of 80,000 results in +/- 1250 or full throw at the output stage when set to 125%
		Config.Raw_I_Limits[i] = temp32 * (int32_t)640;	// 125% * 640 = 80,000

		// I-term source limits. These have to be different due to the I-term gain setting
		// For a gain of 32 and 125%, Constrain = 80,000
		// For a gain of 100 and 125%, Constrain = 32,768
		// For a gain of 10 and 25%, Constrain = 51,200
		// For a gain of 1 and 100%, Constrain = 2,048,000
		// For a gain of 127 and 125%, Constrain = 20,157
		if (gains[i] != 0)
		{
			gain32 = (int32_t)gains[i];
			gain32 = gain32 << 7;				// Multiply divisor by 128
			Config.Raw_I_Constrain[i] = Config.Raw_I_Limits[i] / (gain32 / (int32_t)32);
			Config.Raw_I_Constrain[i] = Config.Raw_I_Constrain[i] << 7; // Restore by multiplying total by 128
		}
		else 
		{
			Config.Raw_I_Constrain[i] = 0;
		}
	}

	// Update travel limits
	for (i = 0; i < MAX_OUTPUTS; i++)
	{
		Config.Limits[i].minimum = scale_percent(Config.min_travel[i]);
		Config.Limits[i].maximum = scale_percent(Config.max_travel[i]);
		Config.Limits[i].failsafe = scale_percent(Config.Failsafe[i]);
		Config.Limits[i].trim = scale_percent(Config.Offset[i]);
	}

	// Update per-channel offsets
	for (i = 0; i < PSUEDO_OUTPUTS; i++)
	{
		Config.PerOffset[i] = scale_percent_nooffset(Config.Channel[i].offset);
	}

	// Update dynamic gain divisor
	if (Config.DynGain > 0)
	{
		Config.DynGainDiv = 2500 / Config.DynGain;
	}
	else
	{
		Config.DynGainDiv = 2500;
	}

	// Update RC deadband amount
	 Config.DeadbandLimit = (Config.Deadband * 12); // 0 to 5% scaled to 0 to 60

	// Update Hands-free trigger based on deadband setting
	Config.HandsFreetrigger = Config.DeadbandLimit;
}

// Update servos from the mixer Config.Channel[i].value data and enforce travel limits
void UpdateServos(void)
{
	uint8_t i;

	for (i = 0; i < MAX_OUTPUTS; i++)
	{
		if (Config.Channel[i].value > Config.Limits[i].maximum)
		{
			ServoOut[i] = Config.Limits[i].maximum;
		}

		else if (Config.Channel[i].value < Config.Limits[i].minimum)
		{
			ServoOut[i] = Config.Limits[i].minimum;
		}
		else
		{
			ServoOut[i] = Config.Channel[i].value;
		}
	}
}

// 32 bit multiply/scale for broken GCC
// Returns immediately if multiplier is 100
int16_t scale32(int16_t value16, int16_t multiplier16)
{
	int32_t temp32 = 0;
	int32_t mult32 = 0;

	// No change if 100% (no scaling)
	if (multiplier16 == 100)
	{
		return value16;
	}

	// Reverse if -100%
	else if (multiplier16 == -100)
	{
		value16 = -value16;	
	}

	// Zero if 0%
	else if (multiplier16 == 0)
	{
		value16 = 0;	
	}

	// Only do the scaling if necessary
	else
	{
		// GCC broken bad regarding multiplying 32 bit numbers, hence all this crap...
		mult32 = multiplier16;
		temp32 = value16;
		temp32 = temp32 * mult32;

		// Divide by 100 to get scaled value
		temp32 = temp32 / (int32_t)100; // I shit you not...
		value16 = (int16_t) temp32;
	}

	return value16;
}

// Scale percentages to position
int16_t scale_percent(int8_t value)
{
	int16_t temp16_1, temp16_2;

	temp16_1 = value; // Promote
	temp16_2 = ((temp16_1 * (int16_t)12) + 3750);

	return temp16_2;
}


// Scale percentages to relative position
int16_t scale_percent_nooffset(int8_t value)
{
	int16_t temp16_1, temp16_2;

	temp16_1 = value; // Promote
	temp16_2 = (temp16_1 * (int16_t)12);

	return temp16_2;
}
