#include <avr/pgmspace.h>

#include "lgtv_keys.h"
#include "avr-cec/cec_keys.h"

PROGMEM const unsigned char cec_keymap[0xf0] = {
	/* All values above 0x7f are invalid CEC keys */
	[0 ... sizeof(cec_keymap) - 1] = 0xff,

	[KEY_CH_UP] =		CEC_KEY_CHANNEL_UP,
	[KEY_CH_DOWN] =		CEC_KEY_CHANNEL_DOWN,
	[KEY_VOL_UP] =		CEC_KEY_VOLUME_UP,
	[KEY_VOL_DOWN] =	CEC_KEY_VOLUME_DOWN,
	[KEY_MUTE] =		CEC_KEY_MUTE,
	[KEY_POWER] =		CEC_KEY_POWER,
	[KEY_INPUT] =		CEC_KEY_INPUT_SELECT,
	[KEY_TV] =		CEC_KEY_TUNE_FUNCTION,
	[KEY_LIVETV] =		CEC_KEY_TUNE_FUNCTION,
	[KEY_0] =		CEC_KEY_0,
	[KEY_1] =		CEC_KEY_1,
	[KEY_2] =		CEC_KEY_2,
	[KEY_3] =		CEC_KEY_3,
	[KEY_4] =		CEC_KEY_4,
	[KEY_5] =		CEC_KEY_5,
	[KEY_6] =		CEC_KEY_6,
	[KEY_7] =		CEC_KEY_7,
	[KEY_8] =		CEC_KEY_8,
	[KEY_9] =		CEC_KEY_9,
	[KEY_FLASHBACK] =	CEC_KEY_PREVIOUS_CHANNEL,
	[KEY_FAV] =		CEC_KEY_NEXT_FAVORITE,

	[KEY_SMART_HOME] =	CEC_KEY_ROOT_MENU,
	[KEY_AUTOCONFIG] =	CEC_KEY_INITIAL_CONFIGURATION,
	[KEY_USER_GUIDE] =	CEC_KEY_HELP,
	[KEY_MYAPPS] =		CEC_KEY_CONTENTS_MENU,
	[KEY_APP] =		CEC_KEY_CONTENTS_MENU,
	[KEY_PREMIUM] =		CEC_KEY_VIDEO_ON_DEMAND,
	[KEY_SOCCER] =		CEC_KEY_FAVORITE_MENU,
	[KEY_XSTUDIO] =		CEC_KEY_DATA,

	[KEY_INPUT_SVID] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_SVID1] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_SVID2] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_AV1] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_AV2] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_RGB1] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_RGB2] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_RGBPC] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_RGBDTV] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_COMP1] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_COMP2] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_COMP3] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_DVI] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_HDMI1] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_HDMI2] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_HDMI3] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_HDMI4] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_VID3] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_VID4] =	CEC_KEY_INPUT_SELECT,
	[KEY_INPUT_TV] =	CEC_KEY_INPUT_SELECT,

	[KEY_RIGHT] =		CEC_KEY_RIGHT,
	[KEY_LEFT] =		CEC_KEY_LEFT,
	[KEY_DOWN] =		CEC_KEY_DOWN,
	[KEY_UP] =		CEC_KEY_UP,
	[KEY_BACK] =		CEC_KEY_EXIT,
	[KEY_EXIT] =		CEC_KEY_CLEAR,
	[KEY_OK] =		CEC_KEY_SELECT,
	[KEY_INFO] =		CEC_KEY_DISPLAY_INFORMATION,

	[KEY_TEXT] =		CEC_KEY_SUB_PICTURE,
	[KEY_TOPT] =		CEC_KEY_SUB_PICTURE,
	[KEY_SUBTITLE] =	CEC_KEY_SUB_PICTURE,
	[KEY_AVMODE] =		CEC_KEY_SELECT_AV_INPUT_FUNCTION,
	[KEY_AUDIO] =		CEC_KEY_SELECT_AUDIO_INPUT_FUNCTION,
	[KEY_SOUND] =		CEC_KEY_SELECT_SOUND_PRESENTATION,
	[KEY_PICTURE] =		CEC_KEY_SELECT_MEDIA_FUNCTION,
	[KEY_RATIO] =		CEC_KEY_ANGLE,
	[KEY_RATIO_ZOOM] =	CEC_KEY_ANGLE,
	[KEY_RATIO_43] =	CEC_KEY_ANGLE,
	[KEY_RATIO_169] =	CEC_KEY_ANGLE,
	[KEY_3D] =		CEC_KEY_ANGLE,
	[KEY_PIP] =		CEC_KEY_ANGLE,
	[KEY_SLEEP] =		CEC_KEY_TIMER_PROGRAMMING,
	[KEY_ENERGY] =		CEC_KEY_TIMER_PROGRAMMING,

	[KEY_F1] =		CEC_KEY_F1,
	[KEY_RED2] =		CEC_KEY_F1,
	[KEY_F2] =		CEC_KEY_F2,
	[KEY_GREEN2] =		CEC_KEY_F2,
	[KEY_F3] =		CEC_KEY_F3,
	[KEY_YELLOW2] =		CEC_KEY_F3,
	[KEY_F4] =		CEC_KEY_F4,
	[KEY_BLUE2] =		CEC_KEY_F4,
	[KEY_QMENU] =		CEC_KEY_MEDIA_CONTEXT_SENSITIVE_MENU,
	[KEY_HOME] =		CEC_KEY_SETUP_MENU,
	[KEY_POP] =		CEC_KEY_SETUP_MENU,
	[KEY_SIMPLINK] =	CEC_KEY_SETUP_MENU,
	[KEY_LIST_ATSC] =	CEC_KEY_ELECTRONIC_PROGRAM_GUIDE,
	[KEY_LIST] =		CEC_KEY_ELECTRONIC_PROGRAM_GUIDE,
	[KEY_GUIDE] =		CEC_KEY_ELECTRONIC_PROGRAM_GUIDE,

	[KEY_FF] =		CEC_KEY_FAST_FORWARD,
	[KEY_FR] =		CEC_KEY_REWIND,
	[KEY_PLAY] =		CEC_KEY_PLAY,
	[KEY_PAUSE] =		CEC_KEY_PAUSE,
	[KEY_STOP] =		CEC_KEY_STOP,
	[KEY_GOTO_PREV] =	CEC_KEY_BACKWARD,
	[KEY_GOTO_NEXT] =	CEC_KEY_FORWARD,
	[KEY_REC] =		CEC_KEY_RECORD,
	[KEY_MC_EJECT] =	CEC_KEY_EJECT,
	[KEY_SAP] =		CEC_KEY_DOT,
};
