#include "rtmpsender.h"

int main()
{
    RtmpSender* sender = RtmpSender::createNew("test.h264", "rtmp://192.168.31.13:1935/live/livestream");
    if(sender->init())
        sender->executeProcess();
    return 0;
}