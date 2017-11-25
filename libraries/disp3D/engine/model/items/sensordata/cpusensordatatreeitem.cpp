//=============================================================================================================
/**
* @file     cpusensordatatreeitem.cpp
* @author   Lars Debor <lars.debor@tu-ilmenau.de>;
*           Matti Hamalainen <msh@nmr.mgh.harvard.edu>
* @version  1.0
* @date     November, 2017
*
* @section  LICENSE
*
* Copyright (C) 2017, Lars Debor and Matti Hamalainen. All rights reserved.
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
* @brief    CpuSensorDataTreeItem class definition.
*
*/


//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "cpusensordatatreeitem.h"
#include "../../workers/rtSensorData/rtsensordataworker.h"
#include <mne/mne_bem_surface.h>
#include "../../../../helpers/interpolation/interpolation.h"
#include "../../../../helpers/geometryinfo/geometryinfo.h"


//*************************************************************************************************************
//=============================================================================================================
// QT INCLUDES
//=============================================================================================================


//*************************************************************************************************************
//=============================================================================================================
// Eigen INCLUDES
//=============================================================================================================


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace Eigen;
using namespace FIFFLIB;
using namespace DISP3DLIB;
using namespace MNELIB;


//*************************************************************************************************************
//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================


//*************************************************************************************************************
//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

CpuSensorDataTreeItem::CpuSensorDataTreeItem(int iType, const QString &text)
    : SensorDataTreeItem(iType, text)
{
    SensorDataTreeItem::initItem();
}


//*************************************************************************************************************

CpuSensorDataTreeItem::~CpuSensorDataTreeItem()
{
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::init(const MatrixX3f& matSurfaceVertColor,
                              const MNEBemSurface& bemSurface,
                              const FiffInfo& fiffInfo,
                              const QString& sSensorType,
                              const double dCancelDist,
                              const QString& sInterpolationFunction)
{
    if(m_bIsDataInit == true)
    {
        qDebug("CpuSensorDataTreeItem::init - Item is already initialized");
    }

    this->setData(0, Data3DTreeModelItemRoles::RTData);

    if(!m_pSensorRtDataWorkController) {
        m_pSensorRtDataWorkController = new RtSensorDataController(true);

        connect(m_pSensorRtDataWorkController, &RtSensorDataController::newRtSmoothedDataAvailable,
                this, &CpuSensorDataTreeItem::onNewRtSmoothedData);
    }


    // map passed sensor type string to fiff constant
    fiff_int_t sensorTypeFiffConstant;
    if (sSensorType.toStdString() == std::string("MEG")) {
        sensorTypeFiffConstant = FIFFV_MEG_CH;
    } else if (sSensorType.toStdString() == std::string("EEG")) {
        sensorTypeFiffConstant = FIFFV_EEG_CH;
    } else {
        qDebug() << "CpuSensorDataTreeItem::init - Unknown sensor type. Returning ...";
        return;
    }

    //fill QVector with the right sensor positions
    QVector<Vector3f> vecSensorPos;
    m_iUsedSensors.clear();
    int iCounter = 0;
    for(const FiffChInfo &info : fiffInfo.chs) {
        //Only take EEG with V as unit or MEG magnetometers with T as unit
        if(info.kind == sensorTypeFiffConstant && (info.unit == FIFF_UNIT_T || info.unit == FIFF_UNIT_V)) {
            vecSensorPos.push_back(info.chpos.r0);

            //save the number of the sensor
            m_iUsedSensors.push_back(iCounter);
        }
        iCounter++;
    }

    //Create bad channel idx list
    for(const QString &bad : fiffInfo.bads) {
        m_iSensorsBad.push_back(fiffInfo.ch_names.indexOf(bad));
    }

    //Set cancle distance
    setCancelDistance(dCancelDist);

    //Set interpolation function
    setInterpolationFunction(sInterpolationFunction);

    m_pSensorRtDataWorkController->setInterpolationInfo(bemSurface,
                                                vecSensorPos,
                                                fiffInfo,
                                                sensorTypeFiffConstant);

    m_pSensorRtDataWorkController->setSurfaceColor(matSurfaceVertColor);

    m_bIsDataInit = true;
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::addData(const MatrixXd &tSensorData)
{
    if(!m_bIsDataInit) {
        qDebug() << "CpuSensorDataTreeItem::addData - sensor data item has not been initialized yet!";
        return;
    }

    //if more data then needed is provided
    const int sensorSize = m_iUsedSensors.size();
    if(tSensorData.rows() > sensorSize)
    {
        MatrixXd dSmallSensorData(sensorSize, tSensorData.cols());
        for(int i = 0 ; i < sensorSize; ++i)
        {
            //Set bad channels to zero so they do not corrupt the histogram thresholding
            if(m_iSensorsBad.contains(m_iUsedSensors[i])) {
                dSmallSensorData.row(i).setZero();
            } else {
                dSmallSensorData.row(i) = tSensorData.row(m_iUsedSensors[i]);
            }
        }

        //Set new data into item's data.
        QVariant data;
        data.setValue(dSmallSensorData);
        this->setData(data, Data3DTreeModelItemRoles::RTData);

        if(m_pSensorRtDataWorkController) {
             m_pSensorRtDataWorkController->addData(dSmallSensorData);
        }
        else {
            qDebug() << "CpuSensorDataTreeItem::addData - worker has not been initialized yet!";
        }
    }
    else
    {
        //Set bad channels to zero so they do not corrupt the histogram thresholding
        MatrixXd dSmallSensorData = tSensorData;
        for(int i = 0 ; i < dSmallSensorData.rows(); ++i)
        {
            if(m_iSensorsBad.contains(m_iUsedSensors[i])) {
                dSmallSensorData.row(i).setZero();
            }
        }

        //Set new data into item's data.
        QVariant data;
        data.setValue(dSmallSensorData);
        this->setData(data, Data3DTreeModelItemRoles::RTData);

        if(m_pSensorRtDataWorkController) {
             m_pSensorRtDataWorkController->addData(dSmallSensorData);
        }
        else {
            qDebug() << "CpuSensorDataTreeItem::addData - worker has not been initialized yet!";
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::setColorOrigin(const MatrixX3f &matVertColor)
{
    if(m_pSensorRtDataWorkController){
        m_pSensorRtDataWorkController->setSurfaceColor(matVertColor);
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::setSFreq(const double dSFreq)
{
    if(m_pSensorRtDataWorkController) {
        m_pSensorRtDataWorkController->setSFreq(dSFreq);
    }
}

//*************************************************************************************************************

void CpuSensorDataTreeItem::updateBadChannels(const FIFFLIB::FiffInfo &info)
{
    if(m_pSensorRtDataWorkController) {
        //Create bad channel idx list
        m_iSensorsBad.clear();
        for(const QString &bad : info.bads) {
            m_iSensorsBad.push_back(info.ch_names.indexOf(bad));
        }

        //qDebug() << "CpuSensorDataTreeItem::updateBadChannels - m_iSensorsBad" << m_iSensorsBad;

        m_pSensorRtDataWorkController->setBadChannels(info);
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onStreamingStateChanged(const Qt::CheckState& checkState)
{
    if(m_pSensorRtDataWorkController) {
        if(checkState == Qt::Checked) {
            m_pSensorRtDataWorkController->setStreamingState(true);
        } else if(checkState == Qt::Unchecked) {
            m_pSensorRtDataWorkController->setStreamingState(false);
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onNewRtSmoothedData(const MatrixX3f &matColorMatrix)
{
    QVariant data;
    data.setValue(matColorMatrix);
    emit rtVertColorChanged(data);
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onColormapTypeChanged(const QVariant& sColormapType)
{
    if(sColormapType.canConvert<QString>()) {
        if(m_pSensorRtDataWorkController) {
            m_pSensorRtDataWorkController->setColormapType(sColormapType.toString());
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onTimeIntervalChanged(const QVariant& iMSec)
{
    if(iMSec.canConvert<int>()) {
        if(m_pSensorRtDataWorkController) {
            m_pSensorRtDataWorkController->setTimeInterval(iMSec.toInt());
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onDataNormalizationValueChanged(const QVariant& vecThresholds)
{
    if(vecThresholds.canConvert<QVector3D>()) {
        if(m_pSensorRtDataWorkController) {
            m_pSensorRtDataWorkController->setThresholds(vecThresholds.value<QVector3D>());
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onCheckStateLoopedStateChanged(const Qt::CheckState& checkState)
{
    if(m_pSensorRtDataWorkController) {
        if(checkState == Qt::Checked) {
            m_pSensorRtDataWorkController->setLoopState(true);
        } else if(checkState == Qt::Unchecked) {
            m_pSensorRtDataWorkController->setLoopState(false);
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onNumberAveragesChanged(const QVariant& iNumAvr)
{
    if(iNumAvr.canConvert<int>()) {
        if(m_pSensorRtDataWorkController) {
            m_pSensorRtDataWorkController->setNumberAverages(iNumAvr.toInt());
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onCancelDistanceChanged(const QVariant &dCancelDist)
{
    if(dCancelDist.canConvert<double>()) {
        if(m_pSensorRtDataWorkController) {
            m_pSensorRtDataWorkController->setCancelDistance(dCancelDist.toDouble());
        }
    }
}


//*************************************************************************************************************

void CpuSensorDataTreeItem::onInterpolationFunctionChanged(const QVariant &sInterpolationFunction)
{
    if(sInterpolationFunction.canConvert<QString>()) {
        if(m_pSensorRtDataWorkController) {
            m_pSensorRtDataWorkController->setInterpolationFunction(sInterpolationFunction.toString());
        }
    }
}


//*************************************************************************************************************
