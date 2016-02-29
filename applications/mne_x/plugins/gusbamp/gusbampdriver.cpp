//=============================================================================================================
/**
* @file     gusbampdriver.cpp
* @author   Viktor Klüber <viktor.klueber@tu-ilmenau.de>;
*           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>;
* @version  1.0
* @date     November, 2015
*
* @section  LICENSE
*
* Copyright (C) 2015, Viktor Klüber, Lorenz Esch and Matti Hamalainen. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that
* the following conditions are met:
*     * Redistributions of source code must retain the above copyright notice, this list of conditions and the
*       following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
*       the following disclaimer in the documentation and/or other materials provided with the distribution.
*     * Neither the name of MNE-CPP authors nor the names of its contributors may be used
*       to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*
* @brief    Contains the implementation of the GUSBAmpDriver class.
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <QtCore>
#include <QtCore/QcoreApplication>
#include <QDebug>
#include "gusbampdriver.h"
#include "gusbampproducer.h"
#include <iostream>
#include <Windows.h>
#include <deque>
#include <stdarg.h>

//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace GUSBAmpPlugin;
using namespace std;

//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

GUSBAmpDriver::GUSBAmpDriver(GUSBAmpProducer* pGUSBAmpProducer)
: m_pGUSBAmpProducer(pGUSBAmpProducer)
, m_NUMBER_OF_CHANNELS(0)
, m_NUMBER_OF_SCANS(0)
, m_SLAVE_SERIALS_SIZE(0)
, m_QUEUE_SIZE(4)
, m_TRIGGER(FALSE)
, m_mode(M_NORMAL)
, m_commonReference({ FALSE, FALSE, FALSE, FALSE })
, m_commonGround({ FALSE, FALSE, FALSE, FALSE })
, m_numBytesReceived(0)
, m_isRunning(false)
, m_bIsWriting(false)
, m_filePath("")
{

    //Linking the specific API-library to the project
    #ifdef _WIN64
        #pragma comment(lib, __FILE__"\\..\\gUSBamp_x64.lib")
    #else
        #pragma comment(lib, __FILE__"\\..\\gUSBamp_x86.lib")
    #endif

    //initialize vector m_sizeOfMatrix with zeros
    m_sizeOfMatrix.resize(2,0);

    //initialize the serial list and setting "UB-2015.05.16" for default
    m_vsSerials.resize(1);
    m_vsSerials[0] = "UB-2015.05.16";

    //setting a deque-list of the serial numbers to be called (LPSTR)
    setSerials(m_vsSerials);

    //initializing UCHAR-list of channels to acquire
    vector<int> channels = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    setChannels(channels);

    //setting Sample parameters and Number of Scans
    setSampleRate(1200);

}


//*************************************************************************************************************

GUSBAmpDriver::~GUSBAmpDriver()
{

    //qDebug() << "GUSBAmpDriver::~GUSBAmpDriver()" << endl;
}


//*************************************************************************************************************

bool GUSBAmpDriver::initDevice()
{

    m_isRunning =true;

    //define Name and place of written data-file
    if(m_bIsWriting)
    {
        m_sFileName = m_filePath;
        m_sFileName.append(QString("data_%1Hz.txt").arg(m_SAMPLE_RATE_HZ));
        m_file.setFileName(m_sFileName);
    }

    //after start is initialized, buffer parameters can be calculated:
    m_nPoints           = m_NUMBER_OF_SCANS * (m_NUMBER_OF_CHANNELS + m_TRIGGER);
    m_bufferSizeBytes   = HEADER_SIZE + m_nPoints * sizeof(float);

    //output of the adjusted parameters:
    qDebug() << "\nFollowing parameters were adjusted:\n";
    qDebug() << "sample rate:\n" <<m_SAMPLE_RATE_HZ << "Hz" ;
    qDebug() << "called device(s):";
    for(deque<LPSTR>::iterator serialNumber = m_callSequenceSerials.begin(); serialNumber != m_callSequenceSerials.end(); serialNumber++)
        qDebug() <<QString(*serialNumber);
    qDebug()<<"size of output Matrix:\n" << m_sizeOfMatrix[0]<<" rows (channels) and "<< m_sizeOfMatrix[1] <<"columns (samples)";
    qDebug()<<"File will be stored under the follwing path:\n" << m_sFileName << endl;


    //load parameters on the device(s)
    try
    {
        for (deque<LPSTR>::iterator serialNumber = m_callSequenceSerials.begin(); serialNumber != m_callSequenceSerials.end(); serialNumber++)
        {
            //open the device
            HANDLE hDevice = GT_OpenDeviceEx(*serialNumber);

            if (hDevice == NULL)
                throw string("Error on GT_OpenDeviceEx: Couldn't open device ").append(*serialNumber);

            //add the device handle to the list of opened devices
            m_openedDevicesHandles.push_back(hDevice);

            //set the channels from that data should be acquired
            if (!GT_SetChannels(hDevice, m_channelsToAcquire, m_NUMBER_OF_CHANNELS))
                throw string("Error on GT_SetChannels: Couldn't set channels to acquire for device ").append(*serialNumber);

            //set the sample rate
            if (!GT_SetSampleRate(hDevice, m_SAMPLE_RATE_HZ))
                throw string("Error on GT_SetSampleRate: Couldn't set sample rate for device ").append(*serialNumber);

            //disable the trigger line
            if (!GT_EnableTriggerLine(hDevice, m_TRIGGER))
                throw string("Error on GT_EnableTriggerLine: Couldn't enable/disable trigger line for device ").append(*serialNumber);

            //set the number of scans that should be received simultaneously
            if (!GT_SetBufferSize(hDevice, m_NUMBER_OF_SCANS))
                throw string("Error on GT_SetBufferSize: Couldn't set the buffer size for device ").append(*serialNumber);

            //don't use bandpass and notch for each channel
            for (int i=0; i<m_NUMBER_OF_CHANNELS; i++)
            {
                //don't use a bandpass filter for any channel
                if (!GT_SetBandPass(hDevice, m_channelsToAcquire[i], -1))
                    throw string("Error on GT_SetBandPass: Couldn't set no bandpass filter for device ").append(*serialNumber);

                //don't use a notch filter for any channel
                if (!GT_SetNotch(hDevice, m_channelsToAcquire[i], -1))
                    throw string("Error on GT_SetNotch: Couldn't set no notch filter for device ").append(*serialNumber);
            }

            //determine master device as the last device in the list
            bool isSlave = (*serialNumber != m_callSequenceSerials.back());

            //set slave/master mode of the device
            if (!GT_SetSlave(hDevice, isSlave))
                throw string("Error on GT_SetSlave: Couldn't set slave/master mode for device ").append(*serialNumber);

            //disable shortcut function
            if (!GT_EnableSC(hDevice, false))
                throw string("Error on GT_EnableSC: Couldn't disable shortcut function for device ").append(*serialNumber);

            //set unipolar derivation
            m_bipolarSettings.Channel1 = 0;
            m_bipolarSettings.Channel2 = 0;
            m_bipolarSettings.Channel3 = 0;
            m_bipolarSettings.Channel4 = 0;
            m_bipolarSettings.Channel5 = 0;
            m_bipolarSettings.Channel6 = 0;
            m_bipolarSettings.Channel7 = 0;
            m_bipolarSettings.Channel8 = 0;
            m_bipolarSettings.Channel9 = 0;
            m_bipolarSettings.Channel10 = 0;
            m_bipolarSettings.Channel11 = 0;
            m_bipolarSettings.Channel12 = 0;
            m_bipolarSettings.Channel13 = 0;
            m_bipolarSettings.Channel14 = 0;
            m_bipolarSettings.Channel15 = 0;
            m_bipolarSettings.Channel16 = 0;

            if (!GT_SetBipolar(hDevice, m_bipolarSettings))
                throw string("Error on GT_SetBipolar: Couldn't set unipolar derivation for device ").append(*serialNumber);

            if (m_mode == M_COUNTER)
                if (!GT_SetMode(hDevice, M_NORMAL))
                    throw string("Error on GT_SetMode: Couldn't set mode M_NORMAL (before mode M_COUNTER) for device ").append(*serialNumber);

            //set acquisition mode
            if (!GT_SetMode(hDevice, m_mode))
                throw string("Error on GT_SetMode: Couldn't set mode for device ").append(*serialNumber);

            //for g.USBamp devices set common ground and common reference
            if (strncmp(*serialNumber, "U", 1) == 0 && (m_mode == M_NORMAL || m_mode == M_COUNTER))
            {
                //don't connect the 4 groups to common reference
                if (!GT_SetReference(hDevice, m_commonReference))
                    throw string("Error on GT_SetReference: Couldn't set common reference for device ").append(*serialNumber);

                //don't connect the 4 groups to common ground
                if (!GT_SetGround(hDevice, m_commonGround))
                    throw string("Error on GT_SetGround: Couldn't set common ground for device ").append(*serialNumber);
            }

            printf("\tg.USBamp %s initialized as %s (#%d in the call sequence)!\n", *serialNumber, (isSlave) ? "slave" : "master", m_openedDevicesHandles.size());

        }

        //define the buffer variables and start the device:
        //create _callSequenceHandles for the sequence of calling the devices (Master has to be the last device to be called!)
        m_callSequenceHandles = m_openedDevicesHandles;
        m_numDevices = (int) m_callSequenceHandles.size();

        m_buffers     = new BYTE**[m_numDevices];
        m_overlapped  = new OVERLAPPED*[m_numDevices];

        //for each device create a number of QUEUE_SIZE data buffers
        for (int deviceIndex=0; deviceIndex<m_numDevices; deviceIndex++)
        {
            m_buffers[deviceIndex] = new BYTE*[m_QUEUE_SIZE];
            m_overlapped[deviceIndex] = new OVERLAPPED[m_QUEUE_SIZE];

            //for each data buffer allocate a number of bufferSizeBytes bytes
            for (int queueIndex=0; queueIndex<m_QUEUE_SIZE; queueIndex++)
            {
                m_buffers[deviceIndex][queueIndex] = new BYTE[m_bufferSizeBytes];
                memset(&(m_overlapped[deviceIndex][queueIndex]), 0, sizeof(OVERLAPPED));

                //create a windows event handle that will be signalled when new data from the device has been received for each data buffer
                m_overlapped[deviceIndex][queueIndex].hEvent = CreateEvent(NULL, false, false, NULL);
            }
        }


        //opening the file for data writing and establish data-stream
        if(m_bIsWriting)
        {
            m_file.open(QIODevice::WriteOnly | QIODevice::Text );
            m_stream.setDevice(&m_file);
        }

        //start the devices (master device must be started at last)
        for (int deviceIndex=0; deviceIndex<m_numDevices; deviceIndex++)
        {
            HANDLE hDevice = m_callSequenceHandles[deviceIndex];

            if (!GT_Start(hDevice))
            {
                //throw string("Error on GT_Start: Couldn't start data acquisition of device.");
                cout << "\tError on GT_Start: Couldn't start data acquisition of device.\n";
                return 0;
            }

            //queue-up the first batch of transfer requests
            for (int queueIndex=0; queueIndex<m_QUEUE_SIZE; queueIndex++)
            {
                if (!GT_GetData(hDevice, m_buffers[deviceIndex][queueIndex], m_bufferSizeBytes, &m_overlapped[deviceIndex][queueIndex]))
                {
                    cout << "\tError on GT_GetData.\n";
                    return 0;
                }
            }
        }

        qDebug() << "Plugin GUSBAmp - INFO - initDevice() - The device has been connected and initialised successfully" << endl;

        return true;

    }
    catch (string& exception)
    {

        //in case an exception occurred, close all opened devices...
        while(!m_openedDevicesHandles.empty())
        {
            GT_CloseDevice(&m_openedDevicesHandles.front());
            qDebug() << "error occurred - Device " << &m_openedDevicesHandles.front()  << "was closed" << endl;
            m_openedDevicesHandles.pop_front();
        }

        m_isRunning = false;

        cout << exception << '\n';

        return false;

    }
}


//*************************************************************************************************************

bool GUSBAmpDriver::uninitDevice()
{

    cout << "Stopping devices and cleaning up..." << "\n";

    //clean up allocated resources for each device
    for (int i=0; i<m_numDevices; i++)
    {
        HANDLE hDevice = m_callSequenceHandles[i];

        //clean up allocated resources for each queue per device
        for (int j=0; j<m_QUEUE_SIZE; j++)
        {
            WaitForSingleObject(m_overlapped[i][j].hEvent, 1000);
            CloseHandle(m_overlapped[i][j].hEvent);
            delete [] m_buffers[i][j];
            qDebug()<< "deleted queue buffer" << j << "successfully";
        }

        //stop device
        GT_Stop(hDevice);
        qDebug() << "stopped " << QString(m_callSequenceSerials[i])<< " successfully" << endl;

        //reset device
        GT_ResetTransfer(hDevice);
        qDebug() << "reseted Transfer of " << QString(m_callSequenceSerials[i])<< " successfully" << endl;

        delete [] m_overlapped[i];
        delete [] m_buffers[i];
    }

    delete [] m_buffers;
    delete [] m_overlapped;

    //closing all devices from the Call-Sequence-Handle
    while (!m_callSequenceHandles.empty())
    {
        //closes each opened device and removes it from the call sequence
        GT_CloseDevice(&m_callSequenceHandles.front());
        m_callSequenceHandles.pop_front();
    }

    //closes all openend Device-Handles
    while(!m_openedDevicesHandles.empty())
    {
        GT_CloseDevice(&m_openedDevicesHandles.front());
        m_openedDevicesHandles.pop_front();
    }

    //close the data file
    if(m_bIsWriting)
        m_file.close();

    m_isRunning = false;

    qDebug() << "Plugin GUSBAmp - INFO - uninitDevice() - Successfully uninitialised the device" << endl;

    return true;
}


//*************************************************************************************************************


bool GUSBAmpDriver::getSampleMatrixValue(MatrixXf& sampleMatrix)
{

    sampleMatrix.setZero(); // Clear matrix - set all elements to zero

    for(int queueIndex=0; queueIndex<m_QUEUE_SIZE; queueIndex++)
    {

        //receive data from each device
        for (int deviceIndex = 0; deviceIndex < m_numDevices; deviceIndex++)
        {
            HANDLE hDevice = m_callSequenceHandles[deviceIndex];

            //wait for notification from the system telling that new data is available
            if (WaitForSingleObject(m_overlapped[deviceIndex][queueIndex].hEvent, 1000) == WAIT_TIMEOUT)
            {
                //throw string("Error on data transfer: timeout occurred.");
                cout << "Error on data transfer: timeout occurred." << "\n";
                return 0;
            }

            //get number of received bytes...
            GetOverlappedResult(hDevice, &m_overlapped[deviceIndex][queueIndex], &m_numBytesReceived, false);

            //...and check if we lost something (number of received bytes must be equal to the previously allocated buffer size)
            if (m_numBytesReceived != m_bufferSizeBytes)
            {
                //throw string("Error on data transfer: samples lost.");
                cout << "Error on data transfer: samples lost." << "\n";
                return 0;
            }
        }

        //store received data from each device in the correct order (that is scan-wise, where one scan includes all channels of all devices) ignoring the header
        //Data is aligned as follows: element at position destBuffer(scanIndex * (m_NUMBER_OF_CHANNELS + m_TRIGGER) + channelIndex) * sizeof(float) + HEADER_SIZE is sample of channel channelIndex (zero-based) of the scan with zero-based scanIndex.
        //channelIndex ranges from 0..numDevices*numChannelsPerDevices where numDevices equals the number of recorded devices and numChannelsPerDevice the number of channels from each of those devices.
        //It is assumed that all devices provide the same number of channels.
        for (int scanIndex = 0; scanIndex < m_NUMBER_OF_SCANS; scanIndex++)
        {
            for (int deviceIndex = 0; deviceIndex < m_numDevices; deviceIndex++)
            {
                for(int channelIndex = 0; channelIndex<m_NUMBER_OF_CHANNELS; channelIndex++)
                {
                    BYTE ByteValue[sizeof(float)];
                    float   FloatValue;

                    for(int i=0;i<sizeof(float);i++)
                    {
                        ByteValue[i] = m_buffers[deviceIndex][queueIndex][(scanIndex * (m_NUMBER_OF_CHANNELS + m_TRIGGER) + channelIndex) * sizeof(float) + HEADER_SIZE + i];
                    }
                    memcpy(&FloatValue, &ByteValue, sizeof(float));

                    //attach float value to stream
                    if(m_bIsWriting)
                        m_stream << FloatValue<< "\t";
                    //store float-value to Matrix
                    sampleMatrix(channelIndex  + deviceIndex*int(m_NUMBER_OF_CHANNELS), scanIndex + queueIndex * m_NUMBER_OF_SCANS) = FloatValue;
                }
            }
            if(m_bIsWriting)
                m_stream << "\n";
        }

        //add new GetData call to the queue replacing the currently received one
        for (int deviceIndex = 0; deviceIndex < m_numDevices; deviceIndex++)
            if (!GT_GetData(m_callSequenceHandles[deviceIndex], m_buffers[deviceIndex][queueIndex], m_bufferSizeBytes, &m_overlapped[deviceIndex][queueIndex]))
            {
                cout << "\tError on GT_GetData.\n";
                return 0;
            }
    }


    return true;
}


//*************************************************************************************************************


bool GUSBAmpDriver::setSerials(vector<QString> &list)
{

    int size = list.size();

    if(m_isRunning)
    {
        cout << "Do not change device-parameters while running the device!\n";
        return false;
    }

    if(size>4)
    {
        cout << "ERROR serSerials: max. for serial numbers can be setted!";
        return false;
    }

    //resize the vectordimensions of the string and byte vectors and convert it to LPSTR(LongPointertoSTRing)
    m_vbSerials.resize(size);
    m_vpSerials.resize(size);
    for(int i = 0; i <size; i++)
    {
        m_vbSerials[i] = list.at(i).toLocal8Bit();  //changing into QByteArray
        m_vpSerials[i] = m_vbSerials.at(i).data();  //creating a pointer on the QByteArray
    }

    m_SLAVE_SERIALS_SIZE = size - 1;

    //closes the former call-sequence-list
    while (!m_callSequenceSerials.empty())
    {
        m_callSequenceSerials.pop_front();
    }

    //defining the new deque-list for data acquisition
    for (int i=1; i<=m_SLAVE_SERIALS_SIZE; i++)
        m_callSequenceSerials.push_back(m_vpSerials[i]);
    //add the master device at the end of the list!
    m_callSequenceSerials.push_back(m_vpSerials[0]);

    //refresh size of output matrix
    m_sizeOfMatrix[0] = (int(m_NUMBER_OF_CHANNELS)*int(1 + m_SLAVE_SERIALS_SIZE));    //number of channels * number of devices (number of channels)
    m_sizeOfMatrix[1] = (int(m_NUMBER_OF_SCANS)*int(m_QUEUE_SIZE));                   //number of the scanned samples * number of the queues (nummber of samples)

    return true;

}

//*************************************************************************************************************

bool GUSBAmpDriver::setSampleRate(int sampleRate)
{
    try
    {
        if(m_isRunning)
            throw string("Do not change device-parameters while running the device!\n");

        //choose the number of scans according to the sample rate (see documentation for further hints)
        switch(sampleRate)
        {
        case 32:    m_NUMBER_OF_SCANS = 1;      break;
        case 64:    m_NUMBER_OF_SCANS = 2;      break;
        case 128:   m_NUMBER_OF_SCANS = 8;      break;
        case 256:   m_NUMBER_OF_SCANS = 16;     break;
        case 512:   m_NUMBER_OF_SCANS = 32;     break;
        case 600:   m_NUMBER_OF_SCANS = 64;     break;
        case 1200:  m_NUMBER_OF_SCANS = 128;    break;
        case 2400:  m_NUMBER_OF_SCANS = 128;    break;
        case 4800:  m_NUMBER_OF_SCANS = 256;    break;
        case 9600:  m_NUMBER_OF_SCANS = 512;    break;
        case 19200: m_NUMBER_OF_SCANS = 512;    break;
        case 38400: m_NUMBER_OF_SCANS = 512;    break;
        default: throw string("Error on setSampleRate(choosed default sample rate of 1200 Hz): please choose between following options:\n"
                              "32, 64, 128, 256, 512, 600, 1200, 2400, 4800, 9600, 19200 and 38400:\n"); break;
        }
        m_SAMPLE_RATE_HZ = sampleRate;
        //qDebug()<<"Sample Rate is setted"<<endl;
    }
    catch(string& exception)
    {
        cout << exception << '\n';
        return false;
    }

    //refresh size of output matrix
    m_sizeOfMatrix[0] = (int(m_NUMBER_OF_CHANNELS)*int(1 + m_SLAVE_SERIALS_SIZE));    //number of channels * number of devices (number of channels)
    m_sizeOfMatrix[1] = (int(m_NUMBER_OF_SCANS)*int(m_QUEUE_SIZE));                   //number of the scanned samples * number of the queues (nummber of samples)

    //cout <<"sample rate [HZ]: \t" <<  m_SAMPLE_RATE_HZ << "\n" << "number of scans [/]:\t" << m_NUMBER_OF_SCANS << "\n";

    return true;

}

//*************************************************************************************************************

bool GUSBAmpDriver::setChannels(vector<int> &list)
{
    if(m_isRunning)
    {
        cout << "Do not change device-parameters while running the device!\n";
        return false;
    }

    int size = list.size();
    if (size > 16)
    {
        cout << "ERROR in GUSBAmpDriver::setChannels: Could not set channels. Size of channel-vector has to be less then 16\n";
        return false;
    }

    //checking if values of vector are ascending and smalller then 17
    int i = 0;
    do
        i++;
    while ((i < size) && (list[i] > list[i - 1]) && (list[i] < 17));

    if (i != size)
    {
        cout << "ERROR in GUSBAmpDriver::setChannels: values of the channels in the vector have to be ascending & less then 16\n";
        return false;
    }

    //filling the UCHAR array with the values from the list
    for(int i = 0; i < size; i++)
    {
        m_channelsToAcquire[i] = UCHAR(list[i]);
        //cout<< "channel " << int( m_channelsToAcquire[i]) << " setted \n";
    }

    m_NUMBER_OF_CHANNELS    = UCHAR(size);

    //refresh size of output matrix
    m_sizeOfMatrix[0] = (int(m_NUMBER_OF_CHANNELS)*int(1 + m_SLAVE_SERIALS_SIZE));    //number of channels * number of devices (number of channels)
    m_sizeOfMatrix[1] = (int(m_NUMBER_OF_SCANS)*int(m_QUEUE_SIZE));                   //number of the scanned samples * number of the queues (number of samples)


    return true;

}

//*************************************************************************************************************

bool GUSBAmpDriver::setFileWriting(bool doFileWriting)
{
    if(m_isRunning)
    {
        cout << "Do not change device-parameters while running the device!\n";
        return false;
    }

    m_bIsWriting = doFileWriting;

    return true;
}



//*************************************************************************************************************

bool GUSBAmpDriver::setFilePath(QString FilePath)
{
    if(m_isRunning)
    {
        cout << "Do not change device-parameters while running the device!\n";
        return false;
    }

    //writing the new file-path and settint the flag for writing
    m_filePath      = FilePath;
    m_bIsWriting    = true;

    return true;
}

//*************************************************************************************************************

vector<int> GUSBAmpDriver::getSizeOfSampleMatrix(void)
{
    return m_sizeOfMatrix;
}




