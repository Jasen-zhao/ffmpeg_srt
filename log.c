/*
读取摄像头设备。
*/

#include <stdio.h>
#include <unistd.h>
#define __STDC_CONSTANT_MACROS
//Linux...
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>


int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
	int				i, videoindex;
	AVCodecContext	*pCodecCtx;//编码
	
	avdevice_register_all();//注册libavdevice
	pFormatCtx = avformat_alloc_context();
 
    //Linux下打开输入设备
	AVInputFormat *ifmt=(AVInputFormat *)av_find_input_format("video4linux2");
	if(avformat_open_input(&pFormatCtx,"/dev/video0",ifmt,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return -1;
	}
    
    //检查是否从输入设备获取输入流
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex=-1;
	for(i=0; i<pFormatCtx->nb_streams; i++){
		if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}
	}
	if(videoindex==-1){
		printf("Couldn't find a video stream.\n");
		return -1;
	}
    
    //为AVCodecContext结构体分配内存
	pCodecCtx = avcodec_alloc_context3(NULL);
    if (pCodecCtx == NULL){  
		printf("Could not allocate AVCodecContext \n");  
		return -1;  
    }
    //将AVCodecParameters结构体中码流参数拷贝到AVCodecContext结构体
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar); 
	AVCodec	*pCodec=(AVCodec *)avcodec_find_decoder(pCodecCtx->codec_id);//查找解码器

	if(pCodec==NULL){
		printf("Codec not found.\n");
		return -1;
	}
    //利用pCodec初始化一个音视频编解码器pCodecCtx（AVCodecContext）
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
		printf("Could not open codec.\n");
		return -1;
	}

	int ret;
	AVPacket *packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    //定义AVFrame
	AVFrame	*pFrame;
	pFrame=av_frame_alloc();
	for (;;) {
        if(av_read_frame(pFormatCtx, packet)>=0){
            if(packet->stream_index==videoindex){
                ret = avcodec_send_packet(pCodecCtx, packet);//解码
                if(ret < 0){
                    printf("Decode Error.\n");
                    return -1;
                }
                av_packet_unref(packet);// 解除引用，防止不必要的内存占用
                while(avcodec_receive_frame(pCodecCtx,pFrame)==0) { //循环获取AVFrame解码数据
                    printf("%lu \n",sizeof(pFrame->data));//不加\n是不会自动刷新输出流,直至缓存被填满。
                }
            }
        }
	}

	av_packet_free(&packet);//释放AVPacket
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;
}