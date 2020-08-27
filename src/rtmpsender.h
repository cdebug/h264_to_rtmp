#ifndef RTMPSENDER_H
#define RTMPSENDER_H
#include <iostream>
#include <string.h>
#include <stdio.h>

#define MAX_BUFF_SIZE 409600

class RtmpSender
{
public:
    ~RtmpSender();
    static RtmpSender* createNew(std::string, std::string);
    bool init();
    void executeProcess();
private:
    RtmpSender(std::string, std::string);
    int getNextFrame(uint8_t*);
    int sendH264Frame(uint8_t*, int, bool, int);
    int SendPacket(unsigned int,uint8_t*,unsigned int,unsigned int);
    int SendVideoSpsPps(uint8_t *pps,int pps_len,uint8_t * sps,int sps_len);

    std::string m_fileName;
    std::string m_rtmpUrl;
    FILE* m_file;
    struct RTMP* m_pRtmp;
    uint8_t m_tmpBuff[MAX_BUFF_SIZE];
};

#endif //RTMPSENDER_H