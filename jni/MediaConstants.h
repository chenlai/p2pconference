
#ifndef MEDIACONSTANTS_H_
#define MEDIACONSTANTS_H_

#include <media/stagefright/MetaData.h>

enum {
    kKeyP2PStart = android::kKeyNodeId, //kKeyNodeId is the last key in MetaData key enum
    kKeyP2PLocalPort = 'lclp', //int32_t
    kKeyP2PRemotePort = 'rmtp', //int32_t
    kKeyP2PLocalIP = 'locIP', //cString
    kKeyP2PRemoteIP = 'rIP',    //cString
    kKeyP2PAudioSampleRate = 'audiosamplr', //int32_t
    kKeyP2PAudioChannelCnt = 'audiochannelCnt',
    kKeyP2PPreviewSurface = 'localS',   //pointer
    kKeyP2PRemoteSurface = 'remoteS',   //pointer
    kKeyP2PVideoQuality = 'vQlty',      //int32_t
    kKeyP2PVideoEnabled = 'viCal',      //int32_t
    kKeyP2PAudioEnabled = 'auCal',      //int32_t
    kKeyP2PAudioEnabledViaCss = 'auCs',      //int32_t
    kKeyP2PAudioCodecs = 'audiocodecs',  //cString
} ;

typedef enum video_quality{
    E_HIGH,
    E_MEDIUM,
    E_LOW
} E_VIDEO_QUALITY;


enum {
	PORT_OUTPUT = 1 // Safe bet?
};


#define M_BITRATE_HIGH      1500000
#define M_BITRATE_MEDIUM    1500000
#define M_BITRATE_LOW       500000

/* Quantization macros */
#define M_QUNATIZATION_PI   15      //For i-frame
#define M_QUNATIZATION_PP   15      //For p-frame

//TBD: Need tune following macros with proper values
/* AVCIntraPeriod macros */
#define M_IDR_PERIOD_HIGH    500   //High quality (high bitrate)
#define M_IDR_PERIOD_MEDIUM  1000     //Medium quality (medium bitrate )
#define M_IDR_PERIOD_LOW     0        //Poor quality (low bitrate) only one IDR frame per encoder session

#define M_NPFRAMES_LOW      30  //High quality (high bitrate)
#define M_NPFRAMES_MEDIUM   150  //Medium quality (medium bitrate )
#define M_NPFRAMES_HIGH     90  //Poor quality (low bitrate)

#endif /* MEDIACONSTANTS_H_ */
