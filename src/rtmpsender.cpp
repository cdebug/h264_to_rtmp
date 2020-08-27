#include "rtmpsender.h"
#include <librtmp/rtmp.h>
#include <librtmp/amf.h>
#include "sps_decode.h"
#include <unistd.h>
#include <thread>

bool startCode3(uint8_t* data)
{
    if(data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)
        return true;
    else
        return false;
}

bool startCode4(uint8_t* data)
{
    if(data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01)
        return true;
    else
        return false;
}

RtmpSender::RtmpSender(std::string fileNmae, std::string url)
{
    m_fileName = fileNmae;
    m_rtmpUrl = url;
}

RtmpSender::~RtmpSender()
{
}

RtmpSender* RtmpSender::createNew(std::string fileName, std::string url)
{
    return new RtmpSender(fileName, url);
}

bool RtmpSender::init()
{
    m_file = fopen(m_fileName.c_str(), "rb");
    m_pRtmp = RTMP_Alloc();
	RTMP_Init(m_pRtmp);
    /*设置URL*/
    char url[100];
    sprintf(url, m_rtmpUrl.c_str());
	if (RTMP_SetupURL(m_pRtmp, url) == FALSE)
	{
		RTMP_Free(m_pRtmp);
		return false;
	}
	/*设置可写,即发布流,这个函数必须在连接前使用,否则无效*/
	RTMP_EnableWrite(m_pRtmp);
	/*连接服务器*/
	if (RTMP_Connect(m_pRtmp, NULL) == FALSE) 
	{
		RTMP_Free(m_pRtmp);
		return false;
	} 
	/*连接流*/
	if (RTMP_ConnectStream(m_pRtmp,0) == FALSE)
	{
        printf("connect stream fail\n");
		RTMP_Close(m_pRtmp);
		RTMP_Free(m_pRtmp);
		return false;
	}
	return true;  
}

void RtmpSender::executeProcess()
{
    uint8_t frameBuff[MAX_BUFF_SIZE];
    uint8_t spsBuff[1024];
    uint8_t ppsBuff[1024];
    int len, num = 0, spsLen, ppsLen, frameRate;
    spsLen = getNextFrame(frameBuff);//sps
    // 解码SPS,获取视频图像宽、高信息   
	int width = 0,height = 0, fps=0;  
	h264_decode_sps(frameBuff,spsLen,width,height,fps);  
    if(fps > 0)
		frameRate = fps; 
	else
		frameRate = 25;
    int timeTick = 1000/frameRate;
    memcpy(spsBuff, frameBuff, spsLen);

    ppsLen = getNextFrame(frameBuff);//pps
    memcpy(ppsBuff, frameBuff, ppsLen);
    while(1)
    {
        len = getNextFrame(frameBuff);
        if(len > 0)
        {
            int naluType = 0;
            if(startCode3(frameBuff))
                naluType = frameBuff[3] & 0x1f;
            else if(startCode4(frameBuff))
                naluType = frameBuff[4] & 0x1f;
            switch (naluType)
            {
            case 5:
                SendVideoSpsPps(spsBuff, spsLen, ppsBuff, ppsLen);
            case 1:
                {
                    int n = sendH264Frame(frameBuff, len, naluType == 5, timeTick*num);
                    num++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(timeTick));
                }
                break;
            case 6:
                sendH264Frame(frameBuff, len, naluType == 5, timeTick*num);
                break;
            default:
                break;
            }
        }
        else break;
    }
}

int RtmpSender::getNextFrame(uint8_t* frameBuff)
{
    int len = fread(m_tmpBuff, 1, MAX_BUFF_SIZE, m_file);
    if(startCode3(m_tmpBuff) || startCode4(m_tmpBuff))
    {
        for(int i = 4; i < len - 4; ++i)
        {
            if(startCode3(m_tmpBuff + i) || startCode4(m_tmpBuff + i))
            {
                memcpy(frameBuff, m_tmpBuff, i);
                fseek(m_file, i - len, SEEK_CUR);
                return i;
            }
        }
    }
    return 0;
}

int RtmpSender::sendH264Frame(uint8_t* data, int len, bool bIFrame, int timestamp)
{
    uint8_t body[MAX_BUFF_SIZE];
    int i = 0;
    if(bIFrame)
        body[i++] = 0x17;
    else
        body[i++] = 0x27;
    //AVC NALU
    body[i++] = 0x01;
    body[i++] = 0x00;
    body[i++] = 0x00;
    body[i++] = 0x00;
    //NALU SIZE
    body[i++] = len >> 24 &0xff;
    body[i++] = len >> 16 &0xff;
    body[i++] = len >> 8 &0xff;
    body[i++] = len &0xff;
    //NALU
    memcpy(body + i, data, len);
    return SendPacket(RTMP_PACKET_TYPE_VIDEO, body, len+i, timestamp);
}

int RtmpSender::SendPacket(unsigned int packType,uint8_t*data,unsigned int len,unsigned int timestamp)  
{  
	RTMPPacket packet;
    //这里有个疑惑 body的大小已经是len了，再加上其他属性，实际的大小应该是大于len，但是分配len的大小不会出问题
	RTMPPacket_Alloc(&packet, /*sizeof(RTMPPacket) + RTMP_MAX_HEADER_SIZE + */len);	
    packet.m_packetType = packType;
    packet.m_nChannel = 0x04;
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_nTimeStamp = timestamp;
    packet.m_nInfoField2 = m_pRtmp->m_stream_id;
    packet.m_nBodySize = len;
    packet.m_hasAbsTimestamp = 0;
    memcpy(packet.m_body, data, len);
    int n = RTMP_SendPacket(m_pRtmp, &packet, true);
    RTMPPacket_Free(&packet);
    return n;
}  

int RtmpSender::SendVideoSpsPps(uint8_t *sps,int sps_len,uint8_t * pps,int pps_len)
{
	RTMPPacket packet;
    RTMPPacket_Alloc(&packet, /*sizeof(RTMPPacket) + RTMP_MAX_HEADER_SIZE +*/ sps_len + pps_len + 16);	
	uint8_t body[4096];
	int i = 0;
	body[i++] = 0x17;
	body[i++] = 0x00;

	body[i++] = 0x00;
	body[i++] = 0x00;
	body[i++] = 0x00;

	/*AVCDecoderConfigurationRecord*/
	body[i++] = 0x01;
	body[i++] = sps[1];
	body[i++] = sps[2];
	body[i++] = sps[3];
	body[i++] = 0xff;

	/*sps*/
	body[i++]   = 0xe1;
	body[i++] = (sps_len >> 8) & 0xff;
	body[i++] = sps_len & 0xff;
	memcpy(&body[i],sps,sps_len);
	i +=  sps_len;

	/*pps*/
	body[i++]   = 0x01;
	body[i++] = (pps_len >> 8) & 0xff;
	body[i++] = (pps_len) & 0xff;
	memcpy(&body[i],pps,pps_len);
	i +=  pps_len;

	packet.m_packetType = RTMP_PACKET_TYPE_VIDEO;
	packet.m_nBodySize = i;
	packet.m_nChannel = 0x04;
	packet.m_nTimeStamp = 0;
	packet.m_hasAbsTimestamp = 0;
	packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
	packet.m_nInfoField2 = m_pRtmp->m_stream_id;
    memcpy(packet.m_body, body, i);

	int nRet = RTMP_SendPacket(m_pRtmp,&packet,TRUE);
	RTMPPacket_Free(&packet);
	return nRet;
}