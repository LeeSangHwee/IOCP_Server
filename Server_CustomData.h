#pragma once
#include"Headers.h"

////////////////////////////// Packet ID //////////////////////////////
#define PKT_TEST_CONNECT 	0xa0000001 // TEST 패킷
#define PKT_TEST_SPAWN 		0xa0000002 // TEST 패킷
#define PKT_TEST_POS 		0xa0000003 // TEST 패킷

////////////////////////////// Packet Data //////////////////////////////
// Vector & Rotation
struct F_gVector
{
	float Pos_x;
	float Pos_y;
	float Pos_z;
};

struct F_gRotator
{
	float Rot_Yaw;
	float Rot_Pitch;
	float Rot_Roll;
};

// Base Packet
struct F_tgPacketHeader
{
	UINT32 PktID;
	UINT16 PktSize;
};

struct F_gUserConnect : public F_tgPacketHeader
{
	UINT16 ConnectCount;
};

struct F_gUserSpawn : public F_tgPacketHeader
{
	UINT16 SpawnCount;
};

struct F_gUserPosRot : public F_tgPacketHeader
{
	int m_iPlayerNumber;
	F_gVector m_UserPos;
	F_gRotator m_UserRot;
};