/*
	Copyright (c) 2013-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
	Author: Fantasy@EasyDarwin.org
*/
#include "epollEvent.h"
#include <sys/errno.h>
#include <sys/time.h>
#include <map>
#include "OSMutex.h"

using namespace std;
#if defined(__linux__)
#define MAX_EPOLL_FD	20000

static int epollfd = 0; 	//epoll ������
static epoll_event* _events = NULL; //epoll�¼���������
static int m_curEventReadPos = 0;  //��ǰ���¼�λ�ã���epoll�¼������е�λ��
static int m_curTotalEvents = 0;   //�ܵ��¼�������ÿ��epoll_wait֮�����
static OSMutex sMaxFDPosMutex;		//��
static bool canEpoll = false;		//�Ƿ����ִ��epoll_wait,����Ƶ��ִ�У��˷�CPU
static map<int ,void *> epollFdmap;//ӳ�� fd�Ͷ�Ӧ��RTSPSession����
/*
������:epollInit
����:��ʼ��epoll������epollfd������epoll�¼������ڴ�
*/
int epollInit()
{
    epollfd = epoll_create(MAX_EPOLL_FD);
    
    if(_events == NULL)
    {
        _events = new epoll_event[MAX_EPOLL_FD];//we only listen the read event
    }
    if(_events == NULL)
    {
        perror("new epoll_event error:");
        exit(1);
    }
    m_curEventReadPos = 0;
    m_curTotalEvents = 0;
	return 0;
}

/*
������:addEpollEvent
����:����һ��epoll�����¼�������1 ����ṹ ����2 �¼�����
*/
int addEpollEvent(struct eventreq *req,int event)
{
    if(req == NULL)
    {
        return -1;
    }
	struct epoll_event ev;
	memset(&ev,0x0,sizeof(ev));

    int ret = -1;
    OSMutexLocker locker(&sMaxFDPosMutex);//��������ֹ�̳߳��еĶ���߳�ִ�иú��������²�������¼�ʧ��
    if(event == EV_RE)
    {
        ev.data.fd = req->er_handle;
        ev.events = EPOLLIN|EPOLLHUP|EPOLLERR;//level triggle
        
        ret = epoll_ctl(epollfd,EPOLL_CTL_ADD,req->er_handle,&ev);        
    }
    else if(event == EV_WR)
    {
        ev.data.fd = req->er_handle;
        ev.events = EPOLLOUT;//level triggle
        
        ret = epoll_ctl(epollfd,EPOLL_CTL_ADD,req->er_handle,&ev); 
    }
    else if(event == EV_RM)
    {
        ret = epoll_ctl(epollfd,EPOLL_CTL_DEL,req->er_handle,NULL);//remove all this fd events
    }
    else//epoll can not listen RESET
    {//we dont needed

    }

    epollFdmap[req->er_handle] = req->er_data;
    canEpoll = true;//ÿ�������¼����������ִ��epoll_wait�������˼����¼�����ζ�Ž�������������Ҫ�����¼������fd��
    return 0;
}

/*
������:deleteEpollEvent
����:ɾ��һ��epoll�����¼�������1 Ҫɾ����fd
*/
int deleteEpollEvent(int& fd)
{
    int ret = -1;
    OSMutexLocker locker(&sMaxFDPosMutex);    
    ret = epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,NULL);//remove all this fd events
    canEpoll = true;//ÿɾ��һ��fd�󣬿�������ִ��epoll_wait��������ɾ������fd�ϳ��ֶ��쳣
    return 0;
}

/*
������:epollwaitevent
����:�ȴ�epoll�����¼��������¼�����
*/
int epollwaitevent()
{
    if(canEpoll == false)
    {
        return -1;
    }
    int nfds = 0;
    int curreadPos = -1;//m_curEventReadPos;//start from 0
    if(m_curTotalEvents <= 0)//��ǰһ��epoll�¼���û�е�ʱ��ִ��epoll_wait
    {        
        m_curTotalEvents = 0;
        m_curEventReadPos = 0;
        nfds = epoll_wait(epollfd,_events,MAX_EPOLL_FD,15000);
        
        if(nfds > 0)
        {
            canEpoll = false;//wait����epoll�¼����ȴ�������wait
            m_curTotalEvents = nfds;
        }
        else if(nfds < 0)
        {

        }
        else if(nfds == 0)
        {
            canEpoll = true;//��һ��wait��ʱ�ˣ���һ�μ���wait
        }
        
    }

    if(m_curTotalEvents)//���¼�������ÿ��ȡһ����ȡ��λ��ͨ��m_curEventReadPos����
    {
        curreadPos = m_curEventReadPos;
        m_curEventReadPos ++;
        if(m_curEventReadPos >= m_curTotalEvents - 1)
        {
             m_curEventReadPos = 0;
             m_curTotalEvents = 0;
        }
    }

    return curreadPos;
}
/*
������:epoll_waitevent
����:�ȴ�һ��epoll�����¼�������һ���¼�������1�������¼���ָ�� ����2 ����
*/
int epoll_waitevent(struct eventreq *req, void* onlyForMOSX)
{    
    int eventPos = -1;
    eventPos = epollwaitevent();
    if(eventPos >= 0)
    {
        req->er_handle = _events[eventPos].data.fd;
        if(_events[eventPos].events == EPOLLIN|| _events[eventPos].events == EPOLLHUP|| _events[eventPos].events == EPOLLERR)
        {
            req->er_eventbits = EV_RE;//we only support read event
        }
        else if(_events[eventPos].events == EPOLLOUT)
        {
            req->er_eventbits = EV_WR;
        }
        req->er_data = epollFdmap[req->er_handle];
        OSMutexLocker locker(&sMaxFDPosMutex);
        deleteEpollEvent(req->er_handle);
        return 0;
    }
    return EINTR;
}

/*
������:epollDestory
����:����epoll��������ʱ��
*/
int epollDestory()
{
    delete[] _events;
    return 0;
}
#endif

