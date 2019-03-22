/* This file is part of COVISE.

   You can use it under the terms of the GNU Lesser General Public License
   version 2.1 or later, see lgpl-2.1.txt.

 * License: LGPL 2+ */

/*
 * inputhdw.cpp
 *
 *  Created on: Dec 9, 2014
 *      Author: svnvlad
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "NatNetDriver.h"
#include <NatNetTypes.h>
#include <NatNetCAPI.h>
#include <NatNetClient.h>

#include <config/CoviseConfig.h>

#include <iostream>
#include <osg/Matrix>

#include <OpenVRUI/osg/mathUtils.h> //for MAKE_EULER_MAT
#include <algorithm> // for min/max

using namespace std;
using namespace covise;
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

// MessageHandler receives NatNet error/debug messages
void NATNET_CALLCONV MessageHandler(Verbosity msgType, const char* msg)
{
	// Optional: Filter out debug messages
	if (msgType < Verbosity_Info)
	{
		return;
	}

	printf("\n[NatNetLib]");

	switch (msgType)
	{
	case Verbosity_Debug:
		printf(" [DEBUG]");
		break;
	case Verbosity_Info:
		printf("  [INFO]");
		break;
	case Verbosity_Warning:
		printf("  [WARN]");
		break;
	case Verbosity_Error:
		printf(" [ERROR]");
		break;
	default:
		printf(" [?????]");
		break;
	}

	printf(": %s\n", msg);
}


void NatNetDriver::initArrays()
{

}

void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData)
{
	NatNetDriver * nd = (NatNetDriver *)pUserData;
	nd->DataHandler(data);
}

// DataHandler receives data from the server
// This function is called by NatNet when a frame of mocap data is available
void NatNetDriver::DataHandler(sFrameOfMocapData* data)
{

	// Software latency here is defined as the span of time between:
	//   a) The reception of a complete group of 2D frames from the camera system (CameraDataReceivedTimestamp)
	// and
	//   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
	//
	// This figure may appear slightly higher than the "software latency" reported in the Motive user interface,
	// because it additionally includes the time spent preparing to stream the data via NatNet.
	const uint64_t softwareLatencyHostTicks = data->TransmitTimestamp - data->CameraDataReceivedTimestamp;
	const double softwareLatencyMillisec = (softwareLatencyHostTicks * 1000) / static_cast<double>(serverDescription.HighResClockFrequency);

	// Transit latency is defined as the span of time between Motive transmitting the frame of data, and its reception by the client (now).
	// The SecondsSinceHostTimestamp method relies on NatNetClient's internal clock synchronization with the server using Cristian's algorithm.
	const double transitLatencyMillisec = nn->SecondsSinceHostTimestamp(data->TransmitTimestamp) * 1000.0;


	int i = 0;

	printf("FrameID : %d\n", data->iFrame);
	printf("Timestamp : %3.2lf\n", data->fTimestamp);
	printf("Software latency : %.2lf milliseconds\n", softwareLatencyMillisec);

	// Only recent versions of the Motive software in combination with ethernet camera systems support system latency measurement.
	// If it's unavailable (for example, with USB camera systems, or during playback), this field will be zero.
	const bool bSystemLatencyAvailable = data->CameraMidExposureTimestamp != 0;

	if (bSystemLatencyAvailable)
	{
		// System latency here is defined as the span of time between:
		//   a) The midpoint of the camera exposure window, and therefore the average age of the photons (CameraMidExposureTimestamp)
		// and
		//   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
		const uint64_t systemLatencyHostTicks = data->TransmitTimestamp - data->CameraMidExposureTimestamp;
		const double systemLatencyMillisec = (systemLatencyHostTicks * 1000) / static_cast<double>(serverDescription.HighResClockFrequency);

		// Client latency is defined as the sum of system latency and the transit time taken to relay the data to the NatNet client.
		// This is the all-inclusive measurement (photons to client processing).
		const double clientLatencyMillisec = nn->SecondsSinceHostTimestamp(data->CameraMidExposureTimestamp) * 1000.0;

		// You could equivalently do the following (not accounting for time elapsed since we calculated transit latency above):
		//const double clientLatencyMillisec = systemLatencyMillisec + transitLatencyMillisec;

		printf("System latency : %.2lf milliseconds\n", systemLatencyMillisec);
		printf("Total client latency : %.2lf milliseconds (transit time +%.2lf ms)\n", clientLatencyMillisec, transitLatencyMillisec);
	}
	else
	{
		printf("Transit latency : %.2lf milliseconds\n", transitLatencyMillisec);
	}

	// FrameOfMocapData params
	bool bIsRecording = ((data->params & 0x01) != 0);
	bool bTrackedModelsChanged = ((data->params & 0x02) != 0);
	if (bIsRecording)
		printf("RECORDING\n");
	if (bTrackedModelsChanged)
		printf("Models Changed.\n");


	// timecode - for systems with an eSync and SMPTE timecode generator - decode to values
	int hour, minute, second, frame, subframe;
	NatNet_DecodeTimecode(data->Timecode, data->TimecodeSubframe, &hour, &minute, &second, &frame, &subframe);
	// decode to friendly string
	char szTimecode[128] = "";
	NatNet_TimecodeStringify(data->Timecode, data->TimecodeSubframe, szTimecode, 128);
	printf("Timecode : %s\n", szTimecode);

	// Rigid Bodies
	printf("Rigid Bodies [Count=%d]\n", data->nRigidBodies);
	if (m_numBodies < data->nRigidBodies)
		m_numBodies = data->nRigidBodies;
	for (i = 0; i < data->nRigidBodies; i++)
	{
		// params
		// 0x01 : bool, rigid body was successfully tracked in this frame
		bool bTrackingValid = data->RigidBodies[i].params & 0x01;

		printf("Rigid Body [ID=%d  Error=%3.2f  Valid=%d]\n", data->RigidBodies[i].ID, data->RigidBodies[i].MeanError, bTrackingValid);
		printf("\tx\ty\tz\tqx\tqy\tqz\tqw\n");
		printf("\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
			data->RigidBodies[i].x,
			data->RigidBodies[i].y,
			data->RigidBodies[i].z,
			data->RigidBodies[i].qx,
			data->RigidBodies[i].qy,
			data->RigidBodies[i].qz,
			data->RigidBodies[i].qw);
	}

	// devices
	printf("Device [Count=%d]\n", data->nDevices);
	for (int iDevice = 0; iDevice < data->nDevices; iDevice++)
	{
		printf("Device %d\n", data->Devices[iDevice].ID);
		for (int iChannel = 0; iChannel < data->Devices[iDevice].nChannels; iChannel++)
		{
			printf("\tChannel %d:\t", iChannel);
			if (data->Devices[iDevice].ChannelData[iChannel].nFrames == 0)
			{
				printf("\tEmpty Frame\n");
			}
			else if (data->Devices[iDevice].ChannelData[iChannel].nFrames != analogSamplesPerMocapFrame)
			{
				printf("\tPartial Frame [Expected:%d   Actual:%d]\n", analogSamplesPerMocapFrame, data->Devices[iDevice].ChannelData[iChannel].nFrames);
			}
			for (int iSample = 0; iSample < data->Devices[iDevice].ChannelData[iChannel].nFrames; iSample++)
				printf("%3.2f\t", data->Devices[iDevice].ChannelData[iChannel].Values[iSample]);
			printf("\n");
		}
	}
}


NatNetDriver::NatNetDriver(const std::string &config)
    : InputDevice(config)
{
    m_isVarying = true;
    m_is6Dof = true;

	unsigned char ver[4];
	NatNet_GetVersion(ver);
	printf("NatNet Sample Client (NatNet ver. %d.%d.%d.%d)\n", ver[0], ver[1], ver[2], ver[3]);

	// Install logging callback
	NatNet_SetLogCallback(MessageHandler);

	// create NatNet client
	nn = new NatNetClient();

	// set the frame callback handler
	nn->SetFrameReceivedCallback(::DataHandler, this);	// this function will receive data from the server

    cout << "Initializing NatNet:" << configPath() << endl;
	m_NatNet_server = coCoviseConfig::getEntry("server", configPath(), "localhost");
	m_NatNet_local = coCoviseConfig::getEntry("local", configPath(), "localhost");

	connectParams.connectionType = ConnectionType_Multicast;
	connectParams.serverAddress = m_NatNet_server.c_str();
	connectParams.localAddress = m_NatNet_local.c_str();


    initArrays();
    //


    m_numBodies = 0;

	int iResult = ConnectClient();
	if (iResult != ErrorCode_OK)
	{
		printf("Error initializing client.  See log for details.  Exiting");
		return;
	}
	else
	{
		printf("Client initialized and ready.\n");
	}
	// Send/receive test request
	void* response;
	int nBytes;
	printf("[SampleClient] Sending Test Request\n");
	iResult = nn->SendMessageAndWait("TestRequest", &response, &nBytes);
	if (iResult == ErrorCode_OK)
	{
		printf("[SampleClient] Received: %s", (char*)response);
	}

	// Retrieve Data Descriptions from Motive
	printf("\n\n[SampleClient] Requesting Data Descriptions...");
	sDataDescriptions* pDataDefs = NULL;
	iResult = nn->GetDataDescriptionList(&pDataDefs);
	if (iResult != ErrorCode_OK || pDataDefs == NULL)
	{
		printf("[SampleClient] Unable to retrieve Data Descriptions.");
	}
	else
	{
		printf("[SampleClient] Received %d Data Descriptions:\n", pDataDefs->nDataDescriptions);
		for (int i = 0; i < pDataDefs->nDataDescriptions; i++)
		{
			printf("Data Description # %d (type=%d)\n", i, pDataDefs->arrDataDescriptions[i].type);
			if (pDataDefs->arrDataDescriptions[i].type == Descriptor_MarkerSet)
			{
				// MarkerSet
				sMarkerSetDescription* pMS = pDataDefs->arrDataDescriptions[i].Data.MarkerSetDescription;
				printf("MarkerSet Name : %s\n", pMS->szName);
				for (int i = 0; i < pMS->nMarkers; i++)
					printf("%s\n", pMS->szMarkerNames[i]);

			}
			else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody)
			{
				// RigidBody
				sRigidBodyDescription* pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
				printf("RigidBody Name : %s\n", pRB->szName);
				printf("RigidBody ID : %d\n", pRB->ID);
				printf("RigidBody Parent ID : %d\n", pRB->parentID);
				printf("Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);
				m_numBodies++;
				if (pRB->MarkerPositions != NULL && pRB->MarkerRequiredLabels != NULL)
				{
					for (int markerIdx = 0; markerIdx < pRB->nMarkers; ++markerIdx)
					{
						const MarkerData& markerPosition = pRB->MarkerPositions[markerIdx];
						const int markerRequiredLabel = pRB->MarkerRequiredLabels[markerIdx];

						printf("\tMarker #%d:\n", markerIdx);
						printf("\t\tPosition: %.2f, %.2f, %.2f\n", markerPosition[0], markerPosition[1], markerPosition[2]);

						if (markerRequiredLabel != 0)
						{
							printf("\t\tRequired active label: %d\n", markerRequiredLabel);
						}
					}
				}
			}
			else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton)
			{
				// Skeleton
				sSkeletonDescription* pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
				printf("Skeleton Name : %s\n", pSK->szName);
				printf("Skeleton ID : %d\n", pSK->skeletonID);
				printf("RigidBody (Bone) Count : %d\n", pSK->nRigidBodies);
				for (int j = 0; j < pSK->nRigidBodies; j++)
				{
					sRigidBodyDescription* pRB = &pSK->RigidBodies[j];
					printf("  RigidBody Name : %s\n", pRB->szName);
					printf("  RigidBody ID : %d\n", pRB->ID);
					printf("  RigidBody Parent ID : %d\n", pRB->parentID);
					printf("  Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);
				}
			}
			else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_ForcePlate)
			{
				// Force Plate
				sForcePlateDescription* pFP = pDataDefs->arrDataDescriptions[i].Data.ForcePlateDescription;
				printf("Force Plate ID : %d\n", pFP->ID);
				printf("Force Plate Serial : %s\n", pFP->strSerialNo);
				printf("Force Plate Width : %3.2f\n", pFP->fWidth);
				printf("Force Plate Length : %3.2f\n", pFP->fLength);
				printf("Force Plate Electrical Center Offset (%3.3f, %3.3f, %3.3f)\n", pFP->fOriginX, pFP->fOriginY, pFP->fOriginZ);
				for (int iCorner = 0; iCorner < 4; iCorner++)
					printf("Force Plate Corner %d : (%3.4f, %3.4f, %3.4f)\n", iCorner, pFP->fCorners[iCorner][0], pFP->fCorners[iCorner][1], pFP->fCorners[iCorner][2]);
				printf("Force Plate Type : %d\n", pFP->iPlateType);
				printf("Force Plate Data Type : %d\n", pFP->iChannelDataType);
				printf("Force Plate Channel Count : %d\n", pFP->nChannels);
				for (int iChannel = 0; iChannel < pFP->nChannels; iChannel++)
					printf("\tChannel %d : %s\n", iChannel, pFP->szChannelNames[iChannel]);
			}
			else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Device)
			{
				// Peripheral Device
				sDeviceDescription* pDevice = pDataDefs->arrDataDescriptions[i].Data.DeviceDescription;
				printf("Device Name : %s\n", pDevice->strName);
				printf("Device Serial : %s\n", pDevice->strSerialNo);
				printf("Device ID : %d\n", pDevice->ID);
				printf("Device Channel Count : %d\n", pDevice->nChannels);
				for (int iChannel = 0; iChannel < pDevice->nChannels; iChannel++)
					printf("\tChannel %d : %s\n", iChannel, pDevice->szChannelNames[iChannel]);
			}
			else
			{
				printf("Unknown data type.");
				// Unknown
			}
		}
	}

    poll(); // try to retrieve  bodies
}


// Establish a NatNet Client connection
int NatNetDriver::ConnectClient()
{
	// Release previous server
	nn->Disconnect();

	// Init Client and connect to NatNet server
	int retCode = nn->Connect(connectParams);
	if (retCode != ErrorCode_OK)
	{
		printf("Unable to connect to server.  Error code: %d. Exiting", retCode);
		return ErrorCode_Internal;
	}
	else
	{
		// connection succeeded

		void* pResult;
		int nBytes = 0;
		ErrorCode ret = ErrorCode_OK;

		// print server info
		memset(&serverDescription, 0, sizeof(serverDescription));
		ret = nn->GetServerDescription(&serverDescription);
		if (ret != ErrorCode_OK || !serverDescription.HostPresent)
		{
			printf("Unable to connect to server. Host not present. Exiting.");
			return 1;
		}
		printf("\n[SampleClient] Server application info:\n");
		printf("Application: %s (ver. %d.%d.%d.%d)\n", serverDescription.szHostApp, serverDescription.HostAppVersion[0],
			serverDescription.HostAppVersion[1], serverDescription.HostAppVersion[2], serverDescription.HostAppVersion[3]);
		printf("NatNet Version: %d.%d.%d.%d\n", serverDescription.NatNetVersion[0], serverDescription.NatNetVersion[1],
			serverDescription.NatNetVersion[2], serverDescription.NatNetVersion[3]);
		printf("Client IP:%s\n", connectParams.localAddress);
		printf("Server IP:%s\n", connectParams.serverAddress);
		printf("Server Name:%s\n", serverDescription.szHostComputerName);

		// get mocap frame rate
		ret = nn->SendMessageAndWait("FrameRate", &pResult, &nBytes);
		if (ret == ErrorCode_OK)
		{
			float fRate = *((float*)pResult);
			printf("Mocap Framerate : %3.2f\n", fRate);
		}
		else
			printf("Error getting frame rate.\n");

		// get # of analog samples per mocap frame of data
		ret = nn->SendMessageAndWait("AnalogSamplesPerMocapFrame", &pResult, &nBytes);
		if (ret == ErrorCode_OK)
		{
			analogSamplesPerMocapFrame = *((int*)pResult);
			printf("Analog Samples Per Mocap Frame : %d\n", analogSamplesPerMocapFrame);
		}
		else
			printf("Error getting Analog frame rate.\n");
	}

	return ErrorCode_OK;
}

//====================END of init section============================

//====================Hardware read methods============

/**
 * @brief NatNetDriver::getNatNetBodyMatrix Gets common NatNet body tracking data
 * @param mat   osg::Matrixd to store the data
 * @param devidx NatNet body index
 * @return 0 if it reads and tracks the body; otherwise returns -1
 */

template <class Data>
bool getNatNetMatrix(osg::Matrix &mat, const Data &d)
{

    if (d.quality < 0.)
        return false;

    mat.set(d.rot[0], d.rot[1], d.rot[2], 0,
            d.rot[3], d.rot[4], d.rot[5], 0,
            d.rot[6], d.rot[7], d.rot[8], 0,
            0, 0, 0, 1);

    osg::Vec3d pos(d.loc[0], d.loc[1], d.loc[2]);
    mat.setTrans(pos);

    return true;
}
bool NatNetDriver::updateHand(size_t idx)
{
	if (ssize_t(idx)>= numBodies())
	{
		std::cout<<"!!! Hand id is out of range: "<<idx<<std::endl;
	}

	return getNatNetMatrix(m_bodyMatrices[idx],*h);
}
bool NatNetDriver::updateBodyMatrix(size_t idx)
{
    
    const NatNet_Body_Type_d *b = dt->getBody(idx);
    return getNatNetMatrix(m_bodyMatrices[idx], *b);
}

bool NatNetDriver::updateFlyStick(size_t idx)
{
    if (ssize_t(idx) >= dt->getNumFlyStick())
    {
        std::cout << "!!!flystick id out of range " << idx << std::endl;
        return false;
    }

    /* NatNet API issue:
    * If a configured and calibrated tracking body won't be tracked durung NatNet init,
    * dt->getBody() will crash, and dt->getNumBody() won't return the right number.
    * NatNet API will think that this device doesn't exist until it's tracked.
    */
    const NatNet_FlyStick_Type_d *f = dt->getFlyStick(idx);

    for (int i = 0; i < f->num_button; ++i)
    {
        m_buttonStates[i + m_buttonBase[idx]] = f->button[i] != 0 ? true : false;
    }

    for (int i = 0; i < f->num_joystick; ++i)
    {
        m_valuatorValues[i + m_valuatorBase[idx]] = f->joystick[i];
    }

    return getNatNetMatrix(m_bodyMatrices[idx], *f);
}

NatNetDriver::~NatNetDriver()
{
    stopLoop();
    dt->stopMeasurement();
    delete dt;
}

//==========================main loop =================

/**
 * @brief NatNetDriver::run ImputHdw main loop
 *
 * Gets the status of the input devices
 */
bool NatNetDriver::poll()
{
    if (!dt)
        return false;

    if (!dt->receive())
    {
        // error messages from example
        // cout<<"NatNet input error!"<<endl;

        if (dt->getLastDataError() == NatNetSDK::ERR_TIMEOUT)
        {
            cout << "--- timeout while waiting for tracking data" << endl;
            //return -1;
        }

        if (dt->getLastDataError() == NatNetSDK::ERR_NET)
        {
            fprintf(stderr, "NatNet Driver error: local UDP port %d already in use\n", m_NatNet_port);
            //return -1;
        }

        if (dt->getLastDataError() == NatNetSDK::ERR_PARSE)
        {
            cout << "--- error while parsing tracking data" << endl;
            //return -1;
        }

        return true;
    }

    if (dt->getNumBody() != m_numBodies || dt->getNumFlyStick() != m_numFlySticks ||
    		dt->getNumHand() != m_numHands )
    {
        m_mutex.lock();
        m_numFlySticks = dt->getNumFlyStick();
        m_numBodies = dt->getNumBody();
        m_bodyBase = m_numFlySticks;
        initArrays();

        m_numHands = dt->getNumHand();
        m_handBase = m_bodyBase + m_numBodies;

        m_bodyBase = coCoviseConfig::getInt("bodyBase", configPath(), m_bodyBase);
        m_flystickBase = coCoviseConfig::getInt("flystickBase", configPath(), m_flystickBase);
        m_handBase = coCoviseConfig::getInt("handBase", configPath(), m_handBase);

        if (m_bodyBase < 0)
        {
            std::cerr << "bodyBase " << m_bodyBase << " below zero" << std::endl;
            m_bodyBase = 0;
        }
        if (m_handBase < 0)
        {
            std::cerr << "handBase " << m_handBase << " below zero" << std::endl;
            m_handBase = 0;
        }
        if (m_flystickBase < 0)
        {
            std::cerr << "flystickBase " << m_flystickBase << " below zero" << std::endl;
            m_flystickBase = 0;
        }
        // FIXME: check that indices don't overlap

        const size_t numMat = std::max(std::max(m_bodyBase+m_numBodies, m_flystickBase+m_numFlySticks), m_handBase+m_numHands);
        m_bodyMatrices.resize(numMat);
        m_bodyMatricesValid.resize(numMat);
        m_mutex.unlock();
    }

    bool valid = false;
    m_mutex.lock();
    for (int i = 0; i < dt->getNumFlyStick(); ++i)
    {
        m_bodyMatricesValid[i] = updateFlyStick(i);
        if (m_bodyMatricesValid[i])
            valid = true;
    }
    m_mutex.unlock();

    m_mutex.lock();
    for (int i = 0; i < dt->getNumBody(); ++i)
    {
        m_bodyMatricesValid[m_bodyBase+i] = updateBodyMatrix(i);
        if (m_bodyMatricesValid[m_bodyBase+i])
            valid = true;
    }
    m_mutex.unlock();

    //hands matrices update

    m_mutex.lock();
	for (int i = 0; i < dt->getNumHand(); ++i)
	{
		m_bodyMatricesValid[m_handBase+i] = updateHand(i);
        if (m_bodyMatricesValid[m_handBase+i])
            valid = true;
    }
    m_valid = valid;
    m_mutex.unlock();

    return true;
}

INPUT_PLUGIN(NatNetDriver)