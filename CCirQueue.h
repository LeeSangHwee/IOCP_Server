// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include"Headers.h"

#define QUEUE_SIZE 20000

/**
 * 
 */
class CCirQueue
{
public:
	CCirQueue();
	~CCirQueue();

	void ClearQueue() { InitQueue(); }
	void InitQueue();
	void InitZeroQueue();

	int OnPutData(char *pData, short recvsize);
	void OnPopData(short popsize);

	F_tgPacketHeader* GetPacket();

protected:
	char *m_pQueue;
	int m_sFront;
	int m_sRear;
};
