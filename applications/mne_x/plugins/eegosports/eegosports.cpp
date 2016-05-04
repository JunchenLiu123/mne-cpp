//=============================================================================================================
/**
* @file     eegosports.cpp
* @author   Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
*           Christoph Dinh <chdinh@nmr.mgh.harvard.edu>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     July, 2014
*
* @section  LICENSE
*
* Copyright (C) 2014, Lorenz Esch, Christoph Dinh and Matti Hamalainen. All rights reserved.
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
* @brief    Contains the implementation of the EEGoSports class.
*
*/

//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "eegosports.h"
#include "eegosportsproducer.h"


//*************************************************************************************************************
//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QtCore/QtPlugin>
#include <QtCore/QTextStream>
#include <QDebug>


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace EEGoSportsPlugin;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

EEGoSports::EEGoSports()
: m_pRMTSA_EEGoSports(0)
, m_qStringResourcePath(qApp->applicationDirPath()+"/mne_x_plugins/resources/eegosports/")
, m_pRawMatrixBuffer_In(0)
, m_pEEGoSportsProducer(new EEGoSportsProducer(this))
, m_dLPAShift(0.01)
, m_dRPAShift(0.01)
, m_dNasionShift(0.06)
, m_sLPA("2LD")
, m_sRPA("2RD")
, m_sNasion("0Z")
{
    // Create record file option action bar item/button
    m_pActionSetupProject = new QAction(QIcon(":/images/database.png"), tr("Setup project"), this);
    m_pActionSetupProject->setStatusTip(tr("Setup project"));
    connect(m_pActionSetupProject, &QAction::triggered, this, &EEGoSports::showSetupProjectDialog);
    addPluginAction(m_pActionSetupProject);

    // Create start recordin action bar item/button
    m_pActionStartRecording = new QAction(QIcon(":/images/record.png"), tr("Start recording data to fif file"), this);
    m_pActionStartRecording->setStatusTip(tr("Start recording data to fif file"));
    connect(m_pActionStartRecording, &QAction::triggered, this, &EEGoSports::showStartRecording);
    addPluginAction(m_pActionStartRecording);
}


//*************************************************************************************************************

EEGoSports::~EEGoSports()
{
    //std::cout << "EEGoSports::~EEGoSports() " << std::endl;

    //If the program is closed while the sampling is in process
    if(this->isRunning())
        this->stop();
}


//*************************************************************************************************************

QSharedPointer<IPlugin> EEGoSports::clone() const
{
    QSharedPointer<EEGoSports> pEEGoSportsClone(new EEGoSports());
    return pEEGoSportsClone;
}


//*************************************************************************************************************

void EEGoSports::init()
{
    m_pRMTSA_EEGoSports = PluginOutputData<NewRealTimeMultiSampleArray>::create(this, "EEGoSports", "EEG output data");

    m_outputConnectors.append(m_pRMTSA_EEGoSports);

    //default values used by the setupGUI class must be set here
    m_iSamplingFreq = 1024;
    m_iNumberOfChannels = 90;
    m_iSamplesPerBlock = 1024;
    m_bWriteToFile = false;
    m_bWriteDriverDebugToFile = false;
    m_bIsRunning = false;
    m_bCheckImpedances = false;

    QDate date;
    m_sOutputFilePath = QString ("%1Sequence_01/Subject_01/%2_%3_%4_EEG_001_raw.fif").arg(m_qStringResourcePath).arg(date.currentDate().year()).arg(date.currentDate().month()).arg(date.currentDate().day());

    m_sElcFilePath = QString("./Resources/3DLayouts/standard_waveguard64_duke.elc");

    m_pFiffInfo = QSharedPointer<FiffInfo>(new FiffInfo());
}


//*************************************************************************************************************

void EEGoSports::unload()
{

}


//*************************************************************************************************************

void EEGoSports::setUpFiffInfo()
{
    //
    //Clear old fiff info data
    //
    m_pFiffInfo->clear();

    //
    //Set number of channels, sampling frequency and high/-lowpass
    //
    m_pFiffInfo->nchan = m_iNumberOfChannels;
    m_pFiffInfo->sfreq = m_iSamplingFreq;
    m_pFiffInfo->highpass = (float)0.001;
    m_pFiffInfo->lowpass = m_iSamplingFreq/2;

    //
    //Read electrode positions from .elc file
    //
    QList<QVector<double> > elcLocation3D;
    QList<QVector<double> > elcLocation2D;
    QString unit;
    QStringList elcChannelNames;

    if(!LayoutLoader::readAsaElcFile(m_sElcFilePath, elcChannelNames, elcLocation3D, elcLocation2D, unit)) {
        qDebug() << "Error: Reading elc file.";
    }

    //qDebug() << elcLocation3D;
    //qDebug() << elcLocation2D;
    //qDebug() << elcChannelNames;

    //The positions read from the asa elc file do not correspond to a RAS coordinate system - use a simple 90° z transformation to fix this
    Matrix3f rotation_z;
    rotation_z = AngleAxisf((float)M_PI/2, Vector3f::UnitZ()); //M_PI/2 = 90°
    QVector3D center_pos;

    for(int i = 0; i<elcLocation3D.size(); i++)
    {
        Vector3f point;
        point << elcLocation3D[i][0], elcLocation3D[i][1] , elcLocation3D[i][2];
        Vector3f point_rot = rotation_z * point;
//        cout<<"point: "<<endl<<point<<endl<<endl;
//        cout<<"matrix: "<<endl<<rotation_z<<endl<<endl;
//        cout<<"point_rot: "<<endl<<point_rot<<endl<<endl;
//        cout<<"-----------------------------"<<endl;
        elcLocation3D[i][0] = point_rot[0];
        elcLocation3D[i][1] = point_rot[1];
        elcLocation3D[i][2] = point_rot[2];

        //Also calculate the center position of the electrode positions in this for routine
        center_pos.setX(center_pos.x() + elcLocation3D[i][0]);
        center_pos.setY(center_pos.y() + elcLocation3D[i][1]);
        center_pos.setZ(center_pos.z() + elcLocation3D[i][2]);
    }

    center_pos.setX(center_pos.x()/elcLocation3D.size());
    center_pos.setY(center_pos.y()/elcLocation3D.size());
    center_pos.setZ(center_pos.z()/elcLocation3D.size());

    //
    //Write electrode positions to the digitizer info in the fiffinfo
    //
    QList<FiffDigPoint> digitizerInfo;

    //Only write the EEG channel positions to the fiff info. The Refa devices have next to the EEG input channels 10 other input channels (Bipolar, Auxilary, Digital, Test)
    int numberEEGCh = 64;

    //Check if channel size by user corresponds with read channel informations from the elc file. If not append zeros and string 'unknown' until the size matches.
    if(numberEEGCh > elcLocation3D.size())
    {
        qDebug()<<"Warning: setUpFiffInfo() - Not enough positions read from the elc file. Filling missing channel names and positions with zeroes and 'unknown' strings.";
        QVector<double> tempA(3, 0.0);
        QVector<double> tempB(2, 0.0);
        int size = numberEEGCh-elcLocation3D.size();
        for(int i = 0; i<size; i++)
        {
            elcLocation3D.push_back(tempA);
            elcLocation2D.push_back(tempB);
            elcChannelNames.append(QString("Unknown"));
        }
    }

    //Append LAP value to digitizer data. Take location of electrode minus 1 cm as approximation.
    FiffDigPoint digPoint;
    int indexLE2 = elcChannelNames.indexOf(m_sLPA);
    digPoint.kind = FIFFV_POINT_CARDINAL;
    digPoint.ident = FIFFV_POINT_LPA;//digitizerInfo.size();

    //Set EEG electrode location - Convert from mm to m
    if(indexLE2!=-1)
    {
        digPoint.r[0] = elcLocation3D[indexLE2][0]*0.001;
        digPoint.r[1] = elcLocation3D[indexLE2][1]*0.001;
        digPoint.r[2] = (elcLocation3D[indexLE2][2]-m_dLPAShift*10)*0.001;
        digitizerInfo.push_back(digPoint);
    }
    else
        cout<<"Plugin TMSI - ERROR - LE2 not found. Check loaded layout."<<endl;

    //Append nasion value to digitizer data. Take location of electrode minus 6 cm as approximation.
    int indexZ1 = elcChannelNames.indexOf(m_sNasion);
    digPoint.kind = FIFFV_POINT_CARDINAL;//FIFFV_POINT_NASION;
    digPoint.ident = FIFFV_POINT_NASION;//digitizerInfo.size();

    //Set EEG electrode location - Convert from mm to m
    if(indexZ1!=-1)
    {
        digPoint.r[0] = elcLocation3D[indexZ1][0]*0.001;
        digPoint.r[1] = elcLocation3D[indexZ1][1]*0.001;
        digPoint.r[2] = (elcLocation3D[indexZ1][2]-m_dNasionShift*10)*0.001;
        digitizerInfo.push_back(digPoint);
    }
    else
        cout<<"Plugin TMSI - ERROR - Z1 not found. Check loaded layout."<<endl;

    //Append RAP value to digitizer data. Take location of electrode minus 1 cm as approximation.
    int indexRE2 = elcChannelNames.indexOf(m_sRPA);
    digPoint.kind = FIFFV_POINT_CARDINAL;
    digPoint.ident = FIFFV_POINT_RPA;//digitizerInfo.size();

    //Set EEG electrode location - Convert from mm to m
    if(indexRE2!=-1)
    {
        digPoint.r[0] = elcLocation3D[indexRE2][0]*0.001;
        digPoint.r[1] = elcLocation3D[indexRE2][1]*0.001;
        digPoint.r[2] = (elcLocation3D[indexRE2][2]-m_dRPAShift*10)*0.001;
        digitizerInfo.push_back(digPoint);
    }
    else
        cout<<"Plugin TMSI - ERROR - RE2 not found. Check loaded layout."<<endl;

    //Add EEG electrode positions as digitizers
    for(int i=0; i<numberEEGCh; i++)
    {
        FiffDigPoint digPoint;
        digPoint.kind = FIFFV_POINT_EEG;
        digPoint.ident = i;

        //Set EEG electrode location - Convert from mm to m
        digPoint.r[0] = elcLocation3D[i][0]*0.001;
        digPoint.r[1] = elcLocation3D[i][1]*0.001;
        digPoint.r[2] = elcLocation3D[i][2]*0.001;
        digitizerInfo.push_back(digPoint);
    }

    //Set the final digitizer values to the fiff info
    m_pFiffInfo->dig = digitizerInfo;

    //
    //Set up the channel info
    //
    QStringList QSLChNames;
    m_pFiffInfo->chs.clear();

    for(int i=0; i<m_iNumberOfChannels; i++)
    {
        //Create information for each channel
        QString sChType;
        FiffChInfo fChInfo;

        //EEG Channels
        if(i<=numberEEGCh-1)
        {
            //Set channel name
            if(!elcChannelNames.empty() && i<elcChannelNames.size()) {
                sChType = QString("EEG ");
                sChType.append(elcChannelNames.at(i));
                fChInfo.ch_name = sChType;
            } else {
                sChType = QString("EEG ");
                if(i<10)
                    sChType.append("00");

                if(i>=10 && i<100)
                    sChType.append("0");

                fChInfo.ch_name = sChType.append(sChType.number(i));
            }

            //Set channel type
            fChInfo.kind = FIFFV_EEG_CH;

            //Set coil type
            fChInfo.coil_type = FIFFV_COIL_EEG;

            //Set logno
            fChInfo.logno = i;

            //Set coord frame
            fChInfo.coord_frame = FIFFV_COORD_HEAD;

            //Set unit
            fChInfo.unit = FIFF_UNIT_V;
            fChInfo.unit_mul = 0;

            //Set EEG electrode location - Convert from mm to m
            fChInfo.eeg_loc(0,0) = elcLocation3D[i][0]*0.001;
            fChInfo.eeg_loc(1,0) = elcLocation3D[i][1]*0.001;
            fChInfo.eeg_loc(2,0) = elcLocation3D[i][2]*0.001;

            //Set EEG electrode direction - Convert from mm to m
            fChInfo.eeg_loc(0,1) = center_pos.x()*0.001;
            fChInfo.eeg_loc(1,1) = center_pos.y()*0.001;
            fChInfo.eeg_loc(2,1) = center_pos.z()*0.001;

            //Also write the eeg electrode locations into the meg loc variable (mne_ex_read_raw() matlab function wants this)
            fChInfo.loc(0,0) = elcLocation3D[i][0]*0.001;
            fChInfo.loc(1,0) = elcLocation3D[i][1]*0.001;
            fChInfo.loc(2,0) = elcLocation3D[i][2]*0.001;

            fChInfo.loc(3,0) = center_pos.x()*0.001;
            fChInfo.loc(4,0) = center_pos.y()*0.001;
            fChInfo.loc(5,0) = center_pos.z()*0.001;

            fChInfo.loc(6,0) = 0;
            fChInfo.loc(7,0) = 1;
            fChInfo.loc(8,0) = 0;

            fChInfo.loc(9,0) = 0;
            fChInfo.loc(10,0) = 0;
            fChInfo.loc(11,0) = 1;

            //cout<<i<<endl<<fChInfo.eeg_loc<<endl;
        }

        //Bipolar channels
        if(i>=64 && i<=87)
        {
            //Set channel type
            fChInfo.kind = FIFFV_MISC_CH;

            sChType = QString("BIPO ");
            fChInfo.ch_name = sChType.append(sChType.number(i-64));
        }

        //Digital input channel
        if(i==88)
        {
            //Set channel type
            fChInfo.kind = FIFFV_STIM_CH;

            sChType = QString("STIM");
            fChInfo.ch_name = sChType;
        }

        //Internally generated test signal - ramp signal
        if(i==89)
        {
            //Set channel type
            fChInfo.kind = FIFFV_MISC_CH;

            sChType = QString("TEST");
            fChInfo.ch_name = sChType;
        }

        QSLChNames << sChType;

        m_pFiffInfo->chs.append(fChInfo);
    }

    //Set channel names in fiff_info_base
    m_pFiffInfo->ch_names = QSLChNames;

    //
    //Set head projection
    //
    m_pFiffInfo->dev_head_t.from = FIFFV_COORD_DEVICE;
    m_pFiffInfo->dev_head_t.to = FIFFV_COORD_HEAD;
    m_pFiffInfo->ctf_head_t.from = FIFFV_COORD_DEVICE;
    m_pFiffInfo->ctf_head_t.to = FIFFV_COORD_HEAD;
}


//*************************************************************************************************************

bool EEGoSports::start()
{
    //Check if the thread is already or still running. This can happen if the start button is pressed immediately after the stop button was pressed. In this case the stopping process is not finished yet but the start process is initiated.
    if(this->isRunning())
        QThread::wait();

    //Setup fiff info
    setUpFiffInfo();

    //Set the channel size of the RMTSA - this needs to be done here and NOT in the init() function because the user can change the number of channels during runtime
    m_pRMTSA_EEGoSports->data()->initFromFiffInfo(m_pFiffInfo);
    m_pRMTSA_EEGoSports->data()->setMultiArraySize(1);//m_iSamplesPerBlock);
    m_pRMTSA_EEGoSports->data()->setSamplingRate(m_iSamplingFreq);

    //Buffer
    m_pRawMatrixBuffer_In = QSharedPointer<RawMatrixBuffer>(new RawMatrixBuffer(8, m_iNumberOfChannels, m_iSamplesPerBlock));
    m_qListReceivedSamples.clear();

    m_pEEGoSportsProducer->start(m_iNumberOfChannels,
                       m_iSamplesPerBlock,
                       m_iSamplingFreq,
                       m_bWriteDriverDebugToFile,
                       m_sOutputFilePath,
                       m_bCheckImpedances);

    if(m_pEEGoSportsProducer->isRunning())
    {
        m_bIsRunning = true;
        QThread::start();
        return true;
    }
    else
    {
        qWarning() << "Plugin EEGoSports - ERROR - EEGoSportsProducer thread could not be started - Either the device is turned off (check your OS device manager) or the driver DLL (EEGO-SDK.dll) is not installed in one of the monitored dll path." << endl;
        return false;
    }
}


//*************************************************************************************************************

bool EEGoSports::stop()
{
    //Stop the producer thread first
    m_pEEGoSportsProducer->stop();

    //Wait until this thread (EEGoSports) is stopped
    m_bIsRunning = false;

    //In case the semaphore blocks the thread -> Release the QSemaphore and let it exit from the pop function (acquire statement)
    m_pRawMatrixBuffer_In->releaseFromPop();

    m_pRawMatrixBuffer_In->clear();

    m_pRMTSA_EEGoSports->data()->clear();

    m_qListReceivedSamples.clear();

    return true;
}


//*************************************************************************************************************

void EEGoSports::setSampleData(MatrixXd &matRawBuffer)
{
    m_mutex.lock();
        m_qListReceivedSamples.append(matRawBuffer);
    m_mutex.unlock();
}


//*************************************************************************************************************

IPlugin::PluginType EEGoSports::getType() const
{
    return _ISensor;
}


//*************************************************************************************************************

QString EEGoSports::getName() const
{
    return "EEGoSports EEG";
}


//*************************************************************************************************************

QWidget* EEGoSports::setupWidget()
{
    EEGoSportsSetupWidget* widget = new EEGoSportsSetupWidget(this);//widget is later destroyed by CentralWidget - so it has to be created everytime new

    //init properties dialog
    widget->initGui();

    return widget;
}


//*************************************************************************************************************

void EEGoSports::onUpdateCardinalPoints(const QString& sLPA, double dLPA, const QString& sRPA, double dRPA, const QString& sNasion, double dNasion)
{
    qDebug()<<"onUpdateCardinalPoints()";

    m_dLPAShift = dLPA;
    m_dRPAShift = dRPA;
    m_dNasionShift = dNasion;

    m_sLPA = sLPA;
    m_sRPA = sRPA;
    m_sNasion = sNasion;
}


//*************************************************************************************************************

void EEGoSports::run()
{
    while(m_bIsRunning)
    {
        if(m_pEEGoSportsProducer->isRunning())
        {
            m_mutex.lock();

            if(m_qListReceivedSamples.isEmpty() == false)
            {
                MatrixXd matValue;
                matValue = m_qListReceivedSamples.first();
                m_qListReceivedSamples.removeFirst();

                //Write raw data to fif file
                if(m_bWriteToFile) {
                    m_pOutfid->write_raw_buffer(matValue, m_cals);
                }

                //emit values to real time multi sample array
                //qDebug()<<"EEGoSports::run() - mat size"<<matValue.rows()<<"x"<<matValue.cols();
                m_pRMTSA_EEGoSports->data()->setValue(matValue);
            }

            m_mutex.unlock();            
        }
    }

    //Close the fif output stream
    if(m_bWriteToFile)
    {
        m_pOutfid->finish_writing_raw();
        m_bWriteToFile = false;
        m_pTimerRecordingChange->stop();
        m_pActionStartRecording->setIcon(QIcon(":/images/record.png"));
    }

    //std::cout<<"EXITING - EEGoSports::run()"<<std::endl;
}


//*************************************************************************************************************

void EEGoSports::showSetupProjectDialog()
{
    // Open setup project widget
    if(m_pEEGoSportsSetupProjectWidget == NULL) {
        m_pEEGoSportsSetupProjectWidget = QSharedPointer<EEGoSportsSetupProjectWidget>(new EEGoSportsSetupProjectWidget(this));

        connect(m_pEEGoSportsSetupProjectWidget.data(), &EEGoSportsSetupProjectWidget::cardinalPointsChanged,
                this, &EEGoSports::onUpdateCardinalPoints);
    }

    if(!m_pEEGoSportsSetupProjectWidget->isVisible())
    {
        m_pEEGoSportsSetupProjectWidget->setWindowTitle("EEGoSports EEG Connector - Setup project");        
        m_pEEGoSportsSetupProjectWidget->show();
        m_pEEGoSportsSetupProjectWidget->raise();
    }
}

//*************************************************************************************************************

void EEGoSports::showStartRecording()
{
    //Setup writing to file
    if(m_bWriteToFile)
    {
        m_pOutfid->finish_writing_raw();
        m_bWriteToFile = false;
        m_pTimerRecordingChange->stop();
        m_pActionStartRecording->setIcon(QIcon(":/images/record.png"));
    }
    else
    {
        if(!m_bIsRunning)
        {
            QMessageBox msgBox;
            msgBox.setText("Start data acquisition first!");
            msgBox.exec();
            return;
        }

        if(!m_pFiffInfo)
        {
            QMessageBox msgBox;
            msgBox.setText("FiffInfo missing!");
            msgBox.exec();
            return;
        }

        //Initiate the stream for writing to the fif file
        m_fileOut.setFileName(m_sOutputFilePath);
        if(m_fileOut.exists())
        {
            QMessageBox msgBox;
            msgBox.setText("The file you want to write already exists.");
            msgBox.setInformativeText("Do you want to overwrite this file?");
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            int ret = msgBox.exec();
            if(ret == QMessageBox::No)
                return;
        }

        // Check if path exists -> otherwise create it
        QStringList list = m_sOutputFilePath.split("/");
        list.removeLast(); // remove file name
        QString fileDir = list.join("/");

        if(!dirExists(fileDir.toStdString()))
        {
            QDir dir;
            dir.mkpath(fileDir);
        }

        m_pOutfid = Fiff::start_writing_raw(m_fileOut, *m_pFiffInfo, m_cals);
        fiff_int_t first = 0;
        m_pOutfid->write_int(FIFF_FIRST_SAMPLE, &first);

        m_bWriteToFile = true;

        m_pTimerRecordingChange = QSharedPointer<QTimer>(new QTimer);
        connect(m_pTimerRecordingChange.data(), &QTimer::timeout, this, &EEGoSports::changeRecordingButton);
        m_pTimerRecordingChange->start(500);
    }
}


//*************************************************************************************************************

void EEGoSports::changeRecordingButton()
{
    if(m_iBlinkStatus == 0)
    {
        m_pActionStartRecording->setIcon(QIcon(":/images/record.png"));
        m_iBlinkStatus = 1;
    }
    else
    {
        m_pActionStartRecording->setIcon(QIcon(":/images/record_active.png"));
        m_iBlinkStatus = 0;
    }
}


//*************************************************************************************************************

bool EEGoSports::dirExists(const std::string& dirName_in)
{
    DWORD ftyp = GetFileAttributesA(dirName_in.c_str());
    if (ftyp == INVALID_FILE_ATTRIBUTES)
        return false;  //something is wrong with your path!

    if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
        return true;   // this is a directory!

    return false;    // this is not a directory!
}


