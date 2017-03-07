//=============================================================================================================
/**
* @file     main.cpp
* @author   Christoph Dinh <christoph.dinh@tu-ilmenau.de>;
*           Lorenz Esch <lorenz.esch@tu-ilmenau.de>
* @version  1.0
* @date     July, 2013
*
* @section  LICENSE
*
* Copyright (C) 2013, Christoph Dinh. All rights reserved.
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
* @brief    Example of MNEForwardSolution and RapMusic application
*
*/


//*************************************************************************************************************
//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <fs/label.h>
#include <fs/surface.h>
#include <fs/surfaceset.h>
#include <fs/annotationset.h>

#include <fiff/fiff_evoked.h>
#include <fiff/fiff.h>

#include <mne/mne.h>
#include <mne/mne_sourceestimate.h>

#include <inverse/rapMusic/rapmusic.h>

#include <disp3D/view3D.h>
#include <disp3D/control/control3dwidget.h>
#include <disp3D/model/data3Dtreemodel.h>
#include <disp3D/model/items/sourceactivity/mneestimatetreeitem.h>

#include <utils/mnemath.h>

#include <iostream>


//*************************************************************************************************************
//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QApplication>
#include <QCommandLineParser>


//*************************************************************************************************************
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace MNELIB;
using namespace FSLIB;
using namespace FIFFLIB;
using namespace INVERSELIB;
using namespace UTILSLIB;
using namespace DISP3DLIB;


//*************************************************************************************************************
//=============================================================================================================
// MAIN
//=============================================================================================================

//=============================================================================================================
/**
* The function main marks the entry point of the program.
* By default, main has the storage class extern.
*
* @param [in] argc (argument count) is an integer that indicates how many arguments were entered on the command line when the program was started.
* @param [in] argv (argument vector) is an array of pointers to arrays of character objects. The array objects are null-terminated strings, representing the arguments that were entered on the command line when the program was started.
* @return the value that was set to exit() (which is 0 if exit() is called via quit()).
*/
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Command Line Parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Compute Inverse Rap Music Example");
    parser.addHelpOption();
    QCommandLineOption fwdFileOption("fwd", "Path to the forward solution <file>.", "file", "./MNE-sample-data/MEG/sample/sample_audvis-meg-eeg-oct-6-fwd.fif");
    QCommandLineOption evokedFileOption("ave", "Path to the evoked/average <file>.", "file", "./MNE-sample-data/MEG/sample/sample_audvis-ave.fif");
    QCommandLineOption subjectDirectoryOption("subjDir", "Path to subject <directory>.", "directory", "./MNE-sample-data/subjects");
    QCommandLineOption subjectOption("subj", "Selected <subject>.", "subject", "sample");
    QCommandLineOption stcFileOption("stcOut", "Path to stc <file>, which is to be written.", "file", "");//"RapMusic.stc");
    QCommandLineOption doMovieOption("doMovie", "Create overlapping movie.", "doMovie", "false");
    QCommandLineOption annotOption("annotType", "Annotation type <type>.", "type", "aparc.a2009s");
    QCommandLineOption numDipolePairsOption("numDip", "<number> of dipole pairs to localize.", "number", "1");
    QCommandLineOption surfOption("surfType", "Surface type <type>.", "type", "orig");

    parser.addOption(fwdFileOption);
    parser.addOption(evokedFileOption);
    parser.addOption(subjectDirectoryOption);
    parser.addOption(subjectOption);
    parser.addOption(stcFileOption);
    parser.addOption(annotOption);
    parser.addOption(numDipolePairsOption);
    parser.addOption(surfOption);
    parser.process(a);

    //Load data
    QFile t_fileFwd(parser.value(fwdFileOption));
    QFile t_fileEvoked(parser.value(evokedFileOption));
    QString subject(parser.value(subjectOption));
    QString subjectDir(parser.value(subjectDirectoryOption));

    AnnotationSet t_annotationSet(subject, 2, parser.value(annotOption), subjectDir);
    SurfaceSet t_surfSet(subject, 2, parser.value(surfOption), subjectDir);

    QString t_sFileNameStc(parser.value(stcFileOption));

    qint32 numDipolePairs = parser.value(numDipolePairsOption).toInt();

    bool doMovie = false;
    if(parser.value(doMovieOption) == "false" || parser.value(doMovieOption) == "0") {
        doMovie = false;
    } else if(parser.value(doMovieOption) == "true" || parser.value(doMovieOption) == "1") {
        doMovie = true;
    }

    qDebug() << "Start calculation with stc:" << t_sFileNameStc;

    // Load data
    fiff_int_t setno = 1;
    QPair<QVariant, QVariant> baseline(QVariant(), 0);
    FiffEvoked evoked(t_fileEvoked, setno, baseline);
    if(evoked.isEmpty())
        return 1;

    std::cout << "evoked first " << evoked.first << "; last " << evoked.last << std::endl;

    MNEForwardSolution t_Fwd(t_fileFwd);
    if(t_Fwd.isEmpty())
        return 1;

    QStringList ch_sel_names = t_Fwd.info.ch_names;
    FiffEvoked pickedEvoked = evoked.pick_channels(ch_sel_names);

    //
    // Cluster forward solution;
    //
    MNEForwardSolution t_clusteredFwd = t_Fwd.cluster_forward_solution(t_annotationSet, 20);//40);

//    std::cout << "Size " << t_clusteredFwd.sol->data.rows() << " x " << t_clusteredFwd.sol->data.cols() << std::endl;
//    std::cout << "Clustered Fwd:\n" << t_clusteredFwd.sol->data.row(0) << std::endl;

    RapMusic t_rapMusic(t_clusteredFwd, false, numDipolePairs);

    int iWinSize = 200;
    if(doMovie) {
        t_rapMusic.setStcAttr(iWinSize, 0.6);
    }

    MNESourceEstimate sourceEstimate = t_rapMusic.calculateInverse(pickedEvoked);

    if(doMovie) {
        //Select only the activations once
        MatrixXd dataPicked(sourceEstimate.data.rows(), int(std::floor(sourceEstimate.data.cols()/iWinSize)));

        for(int i = 0; i < dataPicked.cols(); ++i) {
            dataPicked.col(i) = sourceEstimate.data.col(i*iWinSize);
        }

        sourceEstimate.data = dataPicked;
    }

    //Select only the activations once
    MatrixXd dataPicked(sourceEstimate.data.rows(), int(std::floor(sourceEstimate.data.cols()/iWinSize)));

    for(int i = 0; i < dataPicked.cols(); ++i) {
        dataPicked.col(i) = sourceEstimate.data.col(i*iWinSize);
    }

    sourceEstimate.data = dataPicked;

    if(sourceEstimate.isEmpty())
        return 1;

    //Visualize the results
    View3D::SPtr testWindow = View3D::SPtr(new View3D());
    Data3DTreeModel::SPtr p3DDataModel = Data3DTreeModel::SPtr(new Data3DTreeModel());

    testWindow->setModel(p3DDataModel);

    p3DDataModel->addSurfaceSet(parser.value(subjectOption), evoked.comment, t_surfSet, t_annotationSet);

    //Add rt source loc data and init some visualization values
    if(MneEstimateTreeItem* pRTDataItem = p3DDataModel->addSourceData(parser.value(subjectOption), evoked.comment, sourceEstimate, t_clusteredFwd)) {
        pRTDataItem->setLoopState(true);
        pRTDataItem->setTimeInterval(17);
        pRTDataItem->setNumberAverages(1);
        pRTDataItem->setStreamingActive(true);
        pRTDataItem->setNormalization(QVector3D(0.01,0.5,1.0));
        pRTDataItem->setVisualizationType("Annotation based");
        pRTDataItem->setColortable("Hot");
    }
    testWindow->show();

    Control3DWidget::SPtr control3DWidget = Control3DWidget::SPtr(new Control3DWidget());
    control3DWidget->init(p3DDataModel, testWindow);
    control3DWidget->show();

    if(!t_sFileNameStc.isEmpty())
    {
        QFile t_fileClusteredStc(t_sFileNameStc);
        sourceEstimate.write(t_fileClusteredStc);
    }

    return a.exec();
}