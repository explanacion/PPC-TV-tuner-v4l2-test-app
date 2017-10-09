/* qv4l2: a control panel controlling v4l2 devices.
 *
 * Copyright (C) 2006 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "qv4l2.h"
#include "general-tab.h"
#include "vbi-tab.h"
#include "capture-win.h"

#include <QToolBar>
#include <QToolButton>
#include <QMenuBar>
#include <QFileDialog>
#include <QStatusBar>
#include <QApplication>
#include <QMessageBox>
#include <QLineEdit>
#include <QValidator>
#include <QLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QToolTip>
#include <QImage>
#include <QWhatsThis>
#include <QThread>
#include <QCloseEvent>

#include <assert.h>
#include <sys/mman.h>
#include <errno.h>
#include <dirent.h>
#include <libv4l2.h>

#include <QDebug>
#include <QThreadPool>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <math.h>

int leftchan = 0;
int rightchan = 0;

ApplicationWindow::ApplicationWindow() :
    m_capture(NULL),
    m_sigMapper(NULL)
{
    setAttribute(Qt::WA_DeleteOnClose, true);

    this->resize(this->width() + 350, this->height());
    m_capNotifier = NULL;
    m_capImage = NULL;
    m_frameData = NULL;
    m_nbuffers = 0;
    m_buffers = NULL;
    m_makeSnapshot = false;

    QAction *openAct = new QAction(QIcon(":/fileopen.png"), "&Open Device", this);
    openAct->setStatusTip("Open a v4l device, use libv4l2 wrapper if possible");
    openAct->setShortcut(Qt::CTRL+Qt::Key_O);
    connect(openAct, SIGNAL(triggered()), this, SLOT(opendev()));

    QAction *openRawAct = new QAction(QIcon(":/fileopen.png"), "Open &Raw Device", this);
    openRawAct->setStatusTip("Open a v4l device without using the libv4l2 wrapper");
    openRawAct->setShortcut(Qt::CTRL+Qt::Key_R);
    connect(openRawAct, SIGNAL(triggered()), this, SLOT(openrawdev()));

    m_capStartAct = new QAction(QIcon(":/record.png"), "&Start Capturing", this);
    m_capStartAct->setStatusTip("Start capturing");
    m_capStartAct->setCheckable(true);
    m_capStartAct->setDisabled(true);
    connect(m_capStartAct, SIGNAL(toggled(bool)), this, SLOT(capStart(bool)));

    m_snapshotAct = new QAction(QIcon(":/snapshot.png"), "&Make Snapshot", this);
    m_snapshotAct->setStatusTip("Make snapshot");
    m_snapshotAct->setDisabled(true);
    connect(m_snapshotAct, SIGNAL(triggered()), this, SLOT(snapshot()));

    m_saveRawAct = new QAction(QIcon(":/saveraw.png"), "Save Raw Frames", this);
    m_saveRawAct->setStatusTip("Save raw frames to file.");
    m_saveRawAct->setCheckable(true);
    m_saveRawAct->setChecked(false);
    connect(m_saveRawAct, SIGNAL(toggled(bool)), this, SLOT(capStart2(bool)));
    //connect(m_saveRawAct, SIGNAL(toggled(bool)), this, SLOT(saveRaw(bool)));

    progbar1left = new QProgressBar();
    progbar1right = new QProgressBar();
    progbar1left->setRange(-50,0);
    progbar1right->setRange(-50,0);
    progbar1left->setValue(progbar1left->minimum());
    progbar1right->setValue(progbar1right->minimum());
    progbar1left->setStyleSheet("QProgressBar { height: 15px; border: 2px solid grey; border-radius: 5px; text-align: right; } QProgressBar::chunk {background-color: #05B8CC; }");
    progbar1right->setStyleSheet("QProgressBar { height: 15px; border: 2px solid grey; border-radius: 5px; text-align: right; } QProgressBar::chunk {background-color: #05B8CC; }");
    progbar1left->setFormat("%v dB");
    progbar1right->setFormat("%v dB");
    progbar1left->setTextVisible(false);
    progbar1right->setTextVisible(false);

    gst_init(NULL, NULL);

    m_showFramesAct = new QAction(QIcon(":/video-television.png"), "Show &Frames", this);
    m_showFramesAct->setStatusTip("Only show captured frames if set.");
    m_showFramesAct->setCheckable(true);
    m_showFramesAct->setChecked(true);

    QAction *closeAct = new QAction(QIcon(":/fileclose.png"), "&Close", this);
    closeAct->setStatusTip("Close");
    closeAct->setShortcut(Qt::CTRL+Qt::Key_W);
    connect(closeAct, SIGNAL(triggered()), this, SLOT(closeDevice()));

    QAction *quitAct = new QAction(QIcon(":/exit.png"), "&Quit", this);
    quitAct->setStatusTip("Exit the application");
    quitAct->setShortcut(Qt::CTRL+Qt::Key_Q);
    connect(quitAct, SIGNAL(triggered()), this, SLOT(close()));

    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(openAct);
    fileMenu->addAction(openRawAct);
    fileMenu->addAction(closeAct);
    fileMenu->addAction(m_capStartAct);
    fileMenu->addAction(m_snapshotAct);
    fileMenu->addAction(m_saveRawAct);
    fileMenu->addAction(m_showFramesAct);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAct);

    QToolBar *toolBar = addToolBar("File");
    toolBar->setObjectName("toolBar");
    toolBar->addAction(openAct);
    toolBar->addAction(m_capStartAct);
    toolBar->addAction(m_snapshotAct);
    toolBar->addAction(m_saveRawAct);
    toolBar->addAction(m_showFramesAct);
    toolBar->addSeparator();
    toolBar->addAction(quitAct);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, SLOT(about()), Qt::Key_F1);

    QAction *whatAct = QWhatsThis::createAction(this);
    helpMenu->addAction(whatAct);
    toolBar->addAction(whatAct);

    statusBar()->showMessage("Ready", 2000);

    m_tabs = new QTabWidget;
    m_tabs->setMinimumSize(300, 200);
    setCentralWidget(m_tabs);
}


ApplicationWindow::~ApplicationWindow()
{
    closeDevice();
}


void ApplicationWindow::setDevice(const QString &device, bool rawOpen)
{
    closeDevice();
    m_sigMapper = new QSignalMapper(this);
    connect(m_sigMapper, SIGNAL(mapped(int)), this, SLOT(ctrlAction(int)));

    if (!open(device, !rawOpen))
        return;

    m_capture = new CaptureWin;
    m_capture->setMinimumSize(150, 50);
    connect(m_capture, SIGNAL(close()), this, SLOT(closeCaptureWin()));

    QWidget *w = new QWidget(m_tabs);
    m_genTab = new GeneralTab(device, *this, 4, w, progbar1left, progbar1right);
    m_tabs->addTab(w, "TV"); // new in 2017 rename General tab to TV

    addTabs();
    if (caps() & (V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_CAPTURE)) {
        w = new QWidget(m_tabs);
        m_vbiTab = new VbiTab(w);
        m_tabs->addTab(w, "VBI");
    }
    if (QWidget *current = m_tabs->currentWidget()) {
        current->show();
    }
    statusBar()->clearMessage();
    m_tabs->show();
    m_tabs->setFocus();
    m_convertData = v4lconvert_create(fd());
    m_capStartAct->setEnabled(fd() >= 0 && !m_genTab->isRadio());

    // new in 2017 add a radio tab
    QWidget *rwtab = new QWidget(m_tabs);
    m_tabs->addTab(rwtab, "FM radio");
    //QGridLayout *grid = new QGridLayout(rwtab);
    //grid->setSpacing(3);
    QVBoxLayout *vbox = new QVBoxLayout(rwtab);
    QLabel *lw = new QLabel(rwtab);
    lw->setText("Device: /dev/radio0");

    linefreq = new QLineEdit(rwtab);
    vbox->addWidget(lw);
    vbox->addWidget(linefreq);
    radiofreqtable = new QTableWidget();
    vbox->addWidget(radiofreqtable);
    radiofreqtable->insertColumn(radiofreqtable->columnCount());
    radiofreqtable->setHorizontalHeaderItem(0,new QTableWidgetItem("Station"));
    radiofreqtable->insertColumn(radiofreqtable->columnCount());
    radiofreqtable->setHorizontalHeaderItem(1,new QTableWidgetItem("Frequency, MHz"));
    radiofreqtable->setColumnWidth(0,200);
    radiofreqtable->setColumnWidth(1,200);
    QStringList stations;
    QStringList frequencies;
    stations.append("Comedy"); frequencies.append("92.7");
    stations.append("Energy"); frequencies.append("103.3");
    stations.append("Avtoradio"); frequencies.append("105.2");
    stations.append("Detskoe"); frequencies.append("97.0");
    stations.append("Russkie pesni"); frequencies.append("95.0");
    stations.append("Dorozhnoe"); frequencies.append("100.8");
    stations.append("Retro FM"); frequencies.append("98.7");
    stations.append("Shanson"); frequencies.append("101.7");
    stations.append("Biznes FM"); frequencies.append("104.2");
    stations.append("Love radio"); frequencies.append("101.3");
    stations.append("Evropa+"); frequencies.append("103.8");
    stations.append("Russkoe radio"); frequencies.append("105.8");
    stations.append("Radio MIR"); frequencies.append("97.4");
    stations.append("Radio Dacha"); frequencies.append("104.6");
    stations.append("Komsomolskaya pravda"); frequencies.append("107.1");


    for (int i = 0; i < stations.length(); i++)
    {
        radiofreqtable->insertRow(radiofreqtable->rowCount());
        radiofreqtable->setItem(i,0, new QTableWidgetItem(stations[i]));
        radiofreqtable->setItem(i,1,new QTableWidgetItem(frequencies[i]));
    }

    radiofreqtable->setMinimumWidth(400);

    connect(radiofreqtable,SIGNAL(cellClicked(int,int)),this,SLOT(setradiofreq(int, int)));
    connect((QObject*)radiofreqtable->verticalHeader(),SIGNAL(sectionClicked(int)),this,SLOT(setrowradiofreq(int)));

    // if a tab was changed
    connect(m_tabs, SIGNAL(currentChanged(int)), this, SLOT(tabchanged()));

    progbar2left = new QProgressBar();
    progbar2right = new QProgressBar();

    progbar2left->setRange(-50,0);
    //progbar2left->setInvertedAppearance(true);
    progbar2left->setStyleSheet("QProgressBar { height: 15px; border: 2px solid grey; border-radius: 5px; text-align: right; } QProgressBar::chunk {background-color: #05B8CC; }");
    progbar2left->setValue(progbar2left->minimum());
    progbar2left->setFormat("%v dB");
    progbar2left->setTextVisible(false);


    progbar2right->setRange(-50,0);
    progbar2right->setStyleSheet("QProgressBar { height: 15px; border: 2px solid grey; border-radius: 5px; text-align: right; } QProgressBar::chunk {background-color: #05B8CC; }");
    progbar2right->setFormat("%v dB");
    progbar2right->setValue(progbar2right->minimum());
    progbar2right->setTextVisible(false);

    proglabel = new QLabel();
    proglabel->setText("Audio Level, peak");
    QHBoxLayout *proglayout = new QHBoxLayout();
    QVBoxLayout *channelsproglayout = new QVBoxLayout();
    channelsproglayout->addWidget(progbar2left);
    channelsproglayout->addWidget(progbar2right);
    proglayout->addWidget(proglabel);
    proglayout->addLayout(channelsproglayout);
    vbox->addLayout(proglayout);

}

// new in 2017 tab changed
void ApplicationWindow::tabchanged() {
    int tab = m_tabs->currentIndex();
    if (tab == 2)
    {
        // radio tab
        // disable screenshot button
    }
}

// new in 2017
void ApplicationWindow::setradiofreq(int row, int col) {
    QString selectedvalue = radiofreqtable->item(row,1)->text();
    linefreq->setText(selectedvalue);
//    // if we record video, stop it
//    if (gst_element_get_state(pline,NULL,NULL,-1) == GST_STATE_PLAYING)
//    {
//        stopCapture2();
//    }
//    // set freq here in tuner
//    loop = g_main_loop_new(NULL,FALSE);
//    pline = gst_pipeline_new("tuneraudioset");
//    v4l2radio = gst_element_factory_make("v4l2radio","v4l2radio");
//    g_object_set(G_OBJECT(v4l2radio), "device", "/dev/radio0", NULL);
//    g_object_set(G_OBJECT(v4l2radio), "frequency", selectedvalue, NULL);
//    // write audio

}

void ApplicationWindow::setrowradiofreq(int r) {
    setradiofreq(r, 1);
}

void ApplicationWindow::opendev()
{
    QFileDialog d(this, "Select v4l device", "/dev", "V4L Devices (video* vbi* radio*)");

    d.setFilter(QDir::AllDirs | QDir::Files | QDir::System);
    d.setFileMode(QFileDialog::ExistingFile);
    if (d.exec())
        setDevice(d.selectedFiles().first(), false);
}

void ApplicationWindow::openrawdev()
{
    QFileDialog d(this, "Select v4l device", "/dev", "V4L Devices (video* vbi* radio*)");

    d.setFilter(QDir::AllDirs | QDir::Files | QDir::System);
    d.setFileMode(QFileDialog::ExistingFile);
    if (d.exec())
        setDevice(d.selectedFiles().first(), true);
}

void ApplicationWindow::capVbiFrame()
{
    __u32 buftype = m_genTab->bufType();
    v4l2_buffer buf;
    __u8 *data = NULL;
    int s = 0;
    bool again;

    switch (m_capMethod) {
    case methodRead:
        s = read(m_frameData, m_vbiSize);
        if (s < 0) {
            if (errno != EAGAIN) {
                error("read");
                m_capStartAct->setChecked(false);
            }
            return;
        }
        data = m_frameData;
        break;

    case methodMmap:
        if (!dqbuf_mmap(buf, buftype, again)) {
            error("dqbuf");
            m_capStartAct->setChecked(false);
            return;
        }
        if (again)
            return;
        data = (__u8 *)m_buffers[buf.index].start;
        s = buf.bytesused;
        break;

    case methodUser:
        if (!dqbuf_user(buf, buftype, again)) {
            error("dqbuf");
            m_capStartAct->setChecked(false);
            return;
        }
        if (again)
            return;
        data = (__u8 *)buf.m.userptr;
        s = buf.bytesused;
        break;
    }
    if (buftype == V4L2_BUF_TYPE_VBI_CAPTURE && s != m_vbiSize) {
        error("incorrect vbi size");
        m_capStartAct->setChecked(false);
        return;
    }
    if (m_showFrames) {
        for (unsigned y = 0; y < m_vbiHeight; y++) {
            __u8 *p = data + y * m_vbiWidth;
            __u8 *q = m_capImage->bits() + y * m_capImage->bytesPerLine();

            for (unsigned x = 0; x < m_vbiWidth; x++) {
                *q++ = *p;
                *q++ = *p;
                *q++ = *p++;
            }
        }
    }

    struct v4l2_sliced_vbi_format sfmt;
    struct v4l2_sliced_vbi_data sdata[m_vbiHandle.count[0] + m_vbiHandle.count[1]];
    struct v4l2_sliced_vbi_data *p;

    if (buftype == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) {
        p = (struct v4l2_sliced_vbi_data *)data;
    } else {
        vbi_parse(&m_vbiHandle, data, &sfmt, sdata);
        s = sizeof(sdata);
        p = sdata;
    }

    if (m_capMethod != methodRead)
        qbuf(buf);

    m_vbiTab->slicedData(p, s / sizeof(p[0]));

    QString status, curStatus;
    struct timeval tv, res;

    if (m_frame == 0)
        gettimeofday(&m_tv, NULL);
    gettimeofday(&tv, NULL);
    timersub(&tv, &m_tv, &res);
    if (res.tv_sec) {
        m_fps = (100 * (m_frame - m_lastFrame)) /
            (res.tv_sec * 100 + res.tv_usec / 10000);
        m_lastFrame = m_frame;
        m_tv = tv;
    }

    status = QString("Frame: %1 Fps: %2").arg(++m_frame).arg(m_fps);
    if (m_showFrames)
        m_capture->setImage(*m_capImage, status);
    curStatus = statusBar()->currentMessage();
    if (curStatus.isEmpty() || curStatus.startsWith("Frame: "))
        statusBar()->showMessage(status);
    if (m_frame == 1)
        refresh();
}

// main capture loop
void ApplicationWindow::capFrame()
{
    __u32 buftype = m_genTab->bufType();
    v4l2_buffer buf;
    int s = 0;
    int err = 0;
    bool again;

    switch (m_capMethod) {
    case methodRead:
        s = read(m_frameData, m_capSrcFormat.fmt.pix.sizeimage);
        if (s < 0) {
            if (errno != EAGAIN) {
                error("read");
                m_capStartAct->setChecked(false);
            }
            return;
        }
        if (m_makeSnapshot)
            makeSnapshot((unsigned char *)m_frameData, s);
        if (m_saveRaw.openMode())
            m_saveRaw.write((const char *)m_frameData, s);

        if (!m_showFrames)
            break;
        if (m_mustConvert)
            err = v4lconvert_convert(m_convertData, &m_capSrcFormat, &m_capDestFormat,
                m_frameData, s,
                m_capImage->bits(), m_capDestFormat.fmt.pix.sizeimage);
        if (!m_mustConvert || err < 0)
            memcpy(m_capImage->bits(), m_frameData, std::min(s, m_capImage->numBytes()));
        break;

    case methodMmap:
        if (!dqbuf_mmap(buf, buftype, again)) {
            error("dqbuf");
            m_capStartAct->setChecked(false);
            return;
        }
        if (again)
            return;

        if (m_showFrames) {
            if (m_mustConvert)
                err = v4lconvert_convert(m_convertData,
                    &m_capSrcFormat, &m_capDestFormat,
                    (unsigned char *)m_buffers[buf.index].start, buf.bytesused,
                    m_capImage->bits(), m_capDestFormat.fmt.pix.sizeimage);
            if (!m_mustConvert || err < 0)
                memcpy(m_capImage->bits(),
                       (unsigned char *)m_buffers[buf.index].start,
                       std::min(buf.bytesused, (unsigned)m_capImage->numBytes()));
        }
        if (m_makeSnapshot) {
            makeSnapshot((unsigned char *)m_buffers[buf.index].start, buf.bytesused);
        }
        if (m_saveRaw.openMode()) {
            // videowriter

            m_saveRaw.write((const char *)m_buffers[buf.index].start, buf.bytesused);
        }
        qbuf(buf);
        break;

    case methodUser:
        if (!dqbuf_user(buf, buftype, again)) {
            error("dqbuf");
            m_capStartAct->setChecked(false);
            return;
        }
        if (again)
            return;

        if (m_showFrames) {
            if (m_mustConvert)
                err = v4lconvert_convert(m_convertData,
                    &m_capSrcFormat, &m_capDestFormat,
                    (unsigned char *)buf.m.userptr, buf.bytesused,
                    m_capImage->bits(), m_capDestFormat.fmt.pix.sizeimage);
            if (!m_mustConvert || err < 0)
                memcpy(m_capImage->bits(), (unsigned char *)buf.m.userptr,
                       std::min(buf.bytesused, (unsigned)m_capImage->numBytes()));
        }

        if (m_makeSnapshot)
            makeSnapshot((unsigned char *)buf.m.userptr, buf.bytesused);
        if (m_saveRaw.openMode())
            m_saveRaw.write((const char *)buf.m.userptr, buf.bytesused);

        qbuf(buf);
        break;
    }
    if (err == -1 && m_frame == 0)
        error(v4lconvert_get_error_message(m_convertData));

    QString status, curStatus;
    struct timeval tv, res;

    if (m_frame == 0)
        gettimeofday(&m_tv, NULL);
    gettimeofday(&tv, NULL);
    timersub(&tv, &m_tv, &res);
    if (res.tv_sec) {
        m_fps = (100 * (m_frame - m_lastFrame)) /
            (res.tv_sec * 100 + res.tv_usec / 10000);
        m_lastFrame = m_frame;
        m_tv = tv;
    }
    status = QString("Frame: %1 Fps: %2").arg(++m_frame).arg(m_fps);
    if (m_showFrames)
        m_capture->setImage(*m_capImage, status);
    curStatus = statusBar()->currentMessage();
    if (curStatus.isEmpty() || curStatus.startsWith("Frame: "))
        statusBar()->showMessage(status);
    if (m_frame == 1)
        refresh();
}

bool ApplicationWindow::startCapture(unsigned buffer_size)
{
    __u32 buftype = m_genTab->bufType();
    v4l2_requestbuffers req;
    unsigned int i;

    memset(&req, 0, sizeof(req));

    switch (m_capMethod) {
    case methodRead:
        m_snapshotAct->setEnabled(true);
        /* Nothing to do. */
        return true;

    case methodMmap:
        if (!reqbufs_mmap(req, buftype, 3)) {
            error("Cannot capture");
            break;
        }

        if (req.count < 2) {
            error("Too few buffers");
            reqbufs_mmap(req, buftype);
            break;
        }

        m_buffers = (buffer *)calloc(req.count, sizeof(*m_buffers));

        if (!m_buffers) {
            error("Out of memory");
            reqbufs_mmap(req, buftype);
            break;
        }

        for (m_nbuffers = 0; m_nbuffers < req.count; ++m_nbuffers) {
            v4l2_buffer buf;

            memset(&buf, 0, sizeof(buf));

            buf.type        = buftype;
            buf.memory      = V4L2_MEMORY_MMAP;
            buf.index       = m_nbuffers;

            if (-1 == ioctl(VIDIOC_QUERYBUF, &buf)) {
                perror("VIDIOC_QUERYBUF");
                goto error;
            }

            m_buffers[m_nbuffers].length = buf.length;
            m_buffers[m_nbuffers].start = mmap(buf.length, buf.m.offset);

            if (MAP_FAILED == m_buffers[m_nbuffers].start) {
                perror("mmap");
                goto error;
            }
        }
        for (i = 0; i < m_nbuffers; ++i) {
            if (!qbuf_mmap(i, buftype)) {
                perror("VIDIOC_QBUF");
                goto error;
            }
        }
        if (!streamon(buftype)) {
            perror("VIDIOC_STREAMON");
            goto error;
        }
        m_snapshotAct->setEnabled(true);
        return true;

    case methodUser:
        if (!reqbufs_user(req, buftype, 3)) {
            error("Cannot capture");
            break;
        }

        if (req.count < 2) {
            error("Too few buffers");
            reqbufs_user(req, buftype);
            break;
        }

        m_buffers = (buffer *)calloc(req.count, sizeof(*m_buffers));

        if (!m_buffers) {
            error("Out of memory");
            break;
        }

        for (m_nbuffers = 0; m_nbuffers < req.count; ++m_nbuffers) {
            m_buffers[m_nbuffers].length = buffer_size;
            m_buffers[m_nbuffers].start = malloc(buffer_size);

            if (!m_buffers[m_nbuffers].start) {
                error("Out of memory");
                goto error;
            }
        }
        for (i = 0; i < m_nbuffers; ++i)
            if (!qbuf_user(i, buftype, m_buffers[i].start, m_buffers[i].length)) {
                perror("VIDIOC_QBUF");
                goto error;
            }
        if (!streamon(buftype)) {
            perror("VIDIOC_STREAMON");
            goto error;
        }
        m_snapshotAct->setEnabled(true);
        return true;
    }

error:
    m_capStartAct->setChecked(false);
    return false;
}

void ApplicationWindow::stopCapture()
{
    __u32 buftype = m_genTab->bufType();
    v4l2_requestbuffers reqbufs;
    v4l2_encoder_cmd cmd;
    unsigned i;

    m_snapshotAct->setDisabled(true);
    switch (m_capMethod) {
    case methodRead:
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = V4L2_ENC_CMD_STOP;
        ioctl(VIDIOC_ENCODER_CMD, &cmd);
        break;

    case methodMmap:
        if (m_buffers == NULL)
            break;
        if (!streamoff(buftype))
            perror("VIDIOC_STREAMOFF");
        for (i = 0; i < m_nbuffers; ++i)
            if (-1 == munmap(m_buffers[i].start, m_buffers[i].length))
                perror("munmap");
        // Free all buffers.
        reqbufs_mmap(reqbufs, buftype, 1);  // videobuf workaround
        reqbufs_mmap(reqbufs, buftype, 0);
        break;

    case methodUser:
        if (!streamoff(buftype))
            perror("VIDIOC_STREAMOFF");
        // Free all buffers.
        reqbufs_user(reqbufs, buftype, 1);  // videobuf workaround
        reqbufs_user(reqbufs, buftype, 0);
        for (i = 0; i < m_nbuffers; ++i)
            free(m_buffers[i].start);
        break;
    }
    free(m_buffers);
    m_buffers = NULL;
    refresh();
}

void ApplicationWindow::startOutput(unsigned)
{
}

void ApplicationWindow::stopOutput()
{
}

void ApplicationWindow::closeCaptureWin()
{
    m_capStartAct->setChecked(false);
}

// 2017

static gboolean message_handlermain(GstBus * bus, GstMessage * message, gpointer pbpointer)
{
    if (message->type == GST_MESSAGE_ELEMENT) {
        const GstStructure *s = gst_message_get_structure (message);
        const gchar *name = gst_structure_get_name (s);
        if (strcmp (name, "level") == 0) {

            gint channels;
            gdouble peak_dB;
            const GValue *array_val;
            GValueArray *peak_arr;
            const GValue *value;
            gint i;
            array_val = gst_structure_get_value (s, "peak");
            peak_arr = (GValueArray *) g_value_get_boxed (array_val);

            channels = peak_arr->n_values;
            for (i = 0; i < channels; ++i) {
                value = g_value_array_get_nth (peak_arr, i);
                peak_dB = g_value_get_double (value);
                //g_print("%f\n",peak_dB);
                GetProgBarPointer* pbp = static_cast<GetProgBarPointer*>(pbpointer);
                QProgressBar* progbar2left = static_cast<QProgressBar*>(pbp->leftbar);
                QProgressBar* progbar2right = static_cast<QProgressBar*>(pbp->rightbar);
                if (i == 0) {
                    progbar2left->setTextVisible(true);
                    progbar2left->setValue(peak_dB);
                }
                else if (i == 1) {
                    progbar2right->setTextVisible(true);
                    progbar2right->setValue(peak_dB);
                }
            }

        }
    }
    return TRUE;
}


// catch errors
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_ERROR:
            g_print("Error\n");
            GError *err; err = NULL;
            gchar *dbg_info; dbg_info = NULL;
            gst_message_parse_error (msg, &err, &dbg_info);
            g_printerr ("ERROR from element %s: %s\n",GST_OBJECT_NAME (msg->src), err->message);
            g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
            g_error_free (err);
            g_free (dbg_info);

            g_main_loop_quit(loop);
            break;
        default:
            break;
    }

    return TRUE;
}

static gboolean message_handler (GstBus * bus, GstMessage * message, gpointer pbpointer)
{
    if (message->type == GST_MESSAGE_ELEMENT) {
        const GstStructure *s = gst_message_get_structure (message);
        const gchar *name = gst_structure_get_name (s);
        if (strcmp (name, "level") == 0) {
            gint channels;
            GstClockTime endtime;
            gdouble rms_dB;
            gdouble peak_dB; //, decay_dB;
            //gdouble rms;
            const GValue *array_val;
            const GValue *value;
            GValueArray *rms_arr, *peak_arr, *decay_arr;
            gint i;
            if (!gst_structure_get_clock_time (s, "endtime", &endtime))
                g_warning ("Could not parse endtime");
            /* the values are packed into GValueArrays with the value per channel */
            array_val = gst_structure_get_value (s, "rms");
            rms_arr = (GValueArray *) g_value_get_boxed (array_val);
            array_val = gst_structure_get_value (s, "peak");
            peak_arr = (GValueArray *) g_value_get_boxed (array_val);
            array_val = gst_structure_get_value (s, "decay");
            decay_arr = (GValueArray *) g_value_get_boxed (array_val);
            /* we can get the number of channels as the length of any of the value
                   * arrays */
            channels = rms_arr->n_values;
            //g_print ("endtime: %" GST_TIME_FORMAT ", channels: %d\n", GST_TIME_ARGS (endtime), channels);
            for (i = 0; i < channels; ++i) {
                //g_print ("channel %d\n", i);
                value = g_value_array_get_nth (rms_arr, i);
                rms_dB = g_value_get_double (value);
                value = g_value_array_get_nth (peak_arr, i);
                peak_dB = g_value_get_double (value);

                GetProgBarPointer* pbp = static_cast<GetProgBarPointer*>(pbpointer);

                QProgressBar* progbar2left = static_cast<QProgressBar*>(pbp->leftbar);
                QProgressBar* progbar2right = static_cast<QProgressBar*>(pbp->rightbar);
                // set variables for the indicator
                if (i == 0) {

                    progbar2left->setTextVisible(true);
                    //g_print("%f\n",rms_dB);
                    progbar2left->setValue(peak_dB);

                }
                if (i == 1) {
                    progbar2right->setTextVisible(true);
                    progbar2right->setValue(peak_dB);
                }

                //value = g_value_array_get_nth (decay_arr, i);
                //decay_dB = g_value_get_double (value);
                //g_print ("    RMS: %f dB, peak: %f dB, decay: %f dB\n", rms_dB, peak_dB, decay_dB);
                /* converting from dB to normal gives us a value between 0.0 and 1.0 */
                //rms = pow (10, rms_dB / 20);
                //g_print ("    normalized rms value: %f\n", rms);


            }
        }
    }
    return TRUE;
}


void ApplicationWindow::capStart2(bool start)
{
    int tab = m_tabs->currentIndex();

    if (tab == 0) {
        if (start) {
            loop = g_main_loop_new(NULL,FALSE);
            pline = gst_pipeline_new("tunervid");
            v4l2src = gst_element_factory_make("v4l2src","v4l2src");
            videoqueue = gst_element_factory_make("queue","queue for video");
            alsasrc = gst_element_factory_make("alsasrc","alsasrc");
            audioqueue = gst_element_factory_make("queue","queue for audio");
            videoconvert = gst_element_factory_make("videoconvert","videoconvert");
            audioconvert = gst_element_factory_make("audioconvert","audioconvert");
            videorate = gst_element_factory_make("videorate","videorate");
            theoraenc = gst_element_factory_make("theoraenc","theoraenc");
            vorbisenc = gst_element_factory_make("vorbisenc","vorbisenc");
            oggmux = gst_element_factory_make("oggmux","oggmux");
            filesink = gst_element_factory_make("filesink","filesink");
            level = gst_element_factory_make ("level", "level");
            // set params
            g_object_set(G_OBJECT(v4l2src), "device", "/dev/video0", NULL);
            g_object_set(G_OBJECT(alsasrc), "device","hw:1,0",NULL);
            // get filename
            QString saveto = QFileDialog::getSaveFileName(this, tr("Save video to..."),"~/record.ogg",tr("(*.ogg)"));
            g_object_set(G_OBJECT(filesink), "location",saveto.toStdString().c_str(), NULL);
            g_object_set(G_OBJECT(theoraenc), "quality",63,NULL);
            g_object_set (G_OBJECT (level), "post-messages", TRUE, NULL);

            GstCaps *vcaps;

            vcaps = gst_caps_new_simple ("video/x-raw",
              "bpp",G_TYPE_INT,24,
              "depth",G_TYPE_INT,24,
               "width", G_TYPE_INT, 640,
               "height", G_TYPE_INT, 480,
               NULL);

            //GstPad *sinkpad;
    //        sinkpad = gst_element_get_static_pad(videoconvert,"sink");
    //        gst_pad_set_caps(sinkpad,caps);

            //gboolean vlink_ok;
            //vlink_ok = gst_element_link_filtered(videoconvert,theoraenc, vcaps);
            //gst_caps_unref (vcaps);

            //g_object_set(G_OBJECT(theoraenc), ""

            gst_bin_add_many(GST_BIN (pline), v4l2src, videoqueue, videoconvert, videorate, theoraenc, oggmux, alsasrc, audioqueue, audioconvert, level, vorbisenc, filesink, NULL);

            gst_element_link_many(v4l2src, videoqueue, videoconvert, videorate, theoraenc, oggmux, NULL);
            gst_element_link_many(alsasrc, audioqueue, audioconvert, level, vorbisenc, oggmux, NULL);
            gst_element_link(oggmux,filesink);

            GstCaps *caps;
            caps = gst_caps_from_string("audio/x-raw,format=S16LE,rate=44100,channels=1");

            gboolean link_ok;
            link_ok = gst_element_link_filtered (alsasrc, audioqueue, caps);
            gst_caps_unref (caps);

            bus = gst_pipeline_get_bus(GST_PIPELINE(pline));
            //gst_bus_add_watch(bus, bus_call, loop);
            getpbpointer = new GetProgBarPointer();
            getpbpointer->leftbar = progbar1left;
            getpbpointer->rightbar = progbar1right;

            gst_bus_add_watch(bus,message_handlermain,(gpointer)getpbpointer);
            gst_object_unref(bus);

            gst_element_set_state(pline, GST_STATE_PLAYING);
            g_main_loop_run(loop);


            // build a gstreamer chain
            //GstElement *v4l2 = gst_element_factory_make("v4l2src","video4linuxd");
            //if (!v4l2) {
            //    g_print("Не удалось создать элемент типа 'fakesrc'\n");
            //    return -1;
            //}
            //gchar *name; g_object_get (G_OBJECT (v4l2), "name", &name, NULL);
            //g_print ("Имя элемента: '%s'.\n", name);
            //g_free (name);
            //gst_element_set_state(v4l2,GST_STATE_NULL);

        }
        else {
            delete getpbpointer;
            progbar1left->setValue(progbar1left->minimum());
            progbar1right->setValue(progbar1right->minimum());
            progbar1left->setTextVisible(false);
            progbar1right->setTextVisible(false);
            stopCapture2();
        }
    }
    else if (tab == 2)
    {
        // radio tab
        if (start) {
            // get frequency
            double curfreq = linefreq->text().toDouble();
            guint sfreq = curfreq*1000*1000;
            if (sfreq == 0)
            {
                sfreq = 97.2*1000*1000;
            }
            if (sfreq > 108000000)
                sfreq = 107100000;

            // write audio
            loop2 = g_main_loop_new(NULL,FALSE);
            pline2 = gst_pipeline_new("tuneraudiorecord");
            alsasrc = gst_element_factory_make("alsasrc","alsasrc");
            audioconvert = gst_element_factory_make("audioconvert","audioconvert");
            audioresample = gst_element_factory_make("audioresample","audioresample");
            wavenc = gst_element_factory_make("wavenc","wavenc");
            filesink = gst_element_factory_make("filesink","filesink");
            level = gst_element_factory_make ("level", "level");
            g_assert (level);

            v4l2radio = gst_element_factory_make("v4l2radio","v4l2radio");
            g_object_set(G_OBJECT(v4l2radio), "device", "/dev/radio0", NULL);
            g_object_set(G_OBJECT(v4l2radio), "frequency", sfreq, NULL);

            g_object_set(G_OBJECT(alsasrc), "device","hw:1,0",NULL);
            g_object_set (G_OBJECT (level), "post-messages", TRUE, NULL);

            QString saveto = QFileDialog::getSaveFileName(this, tr("Save video to..."),"record.wav",tr("(*.wav)"));

            g_object_set(G_OBJECT(filesink), "location",saveto.toStdString().c_str(), NULL);

            gst_bin_add_many(GST_BIN(pline2), alsasrc, audioconvert, level, audioresample, wavenc, filesink, NULL);
            gst_element_link_many(alsasrc, audioconvert, level, audioresample, wavenc, filesink, NULL);
            bus = gst_pipeline_get_bus(GST_PIPELINE(pline2));
            getpbpointer = new GetProgBarPointer();
            getpbpointer->leftbar = (gpointer)progbar2left;
            getpbpointer->rightbar = (gpointer)progbar2right;
            guint watch_id = gst_bus_add_watch (bus, message_handler, (gpointer)getpbpointer);
            gst_bus_add_watch(bus, bus_call, loop2);
            gst_object_unref(bus);

            pline = gst_pipeline_new("tuneraudiosetfreq");
            gst_bin_add(GST_BIN(pline),v4l2radio);
            gst_element_set_state(pline, GST_STATE_PLAYING);
            gst_element_link(pline,pline2);

            gst_element_set_state(pline2, GST_STATE_PLAYING);
            g_main_loop_run(loop2);
            g_source_remove (watch_id);
            g_main_loop_unref (loop2);
        }
        else {
            gst_element_send_event(pline2,gst_event_new_eos());
            gst_element_set_state(pline,GST_STATE_NULL);
            gst_element_set_state(pline2,GST_STATE_NULL);
            progbar2left->setValue(progbar2left->minimum());
            progbar2right->setValue(progbar2right->minimum());
            delete getpbpointer;
            g_main_loop_quit(loop2);
        }
    }
}

void ApplicationWindow::stopCapture2()
{
    g_main_loop_quit(loop);
    gst_element_set_state(pline,GST_STATE_NULL);
    // gst_object_unref
}

void ApplicationWindow::capStart(bool start)
{
    int tab = m_tabs->currentIndex();
    if (tab == 0) {
        if (start) {
            // new capture via gstreamer api
            loop2 = g_main_loop_new(NULL,FALSE);
            pline = gst_pipeline_new("tunervideoplayimage");
            pline2 = gst_pipeline_new("tunervideoplaysound");
            v4l2src = gst_element_factory_make("v4l2src","v4l2src");
            xvimagesink = gst_element_factory_make("xvimagesink","xvimagesink");
            alsasrc = gst_element_factory_make("alsasrc","alsasrc");
            level = gst_element_factory_make ("level", "level");
            audioconvert = gst_element_factory_make("audioconvert","audioconvert");
            alsasink = gst_element_factory_make("alsasink","alsasink");

            g_object_set(G_OBJECT(v4l2src), "device", "/dev/video0", NULL);
            g_object_set(G_OBJECT(alsasrc), "device","hw:1,0",NULL);
            g_object_set (G_OBJECT (level), "post-messages", TRUE, NULL);

            gst_bin_add_many(GST_BIN(pline),v4l2src,xvimagesink,NULL);
            gst_bin_add_many(GST_BIN(pline2),alsasrc,audioconvert,level,alsasink,NULL);

            gst_element_link_many(v4l2src,xvimagesink,NULL);
            gst_element_link_many(alsasrc,audioconvert,level,alsasink,NULL);
            bus = gst_pipeline_get_bus(GST_PIPELINE(pline2));
            getpbpointer = new GetProgBarPointer();
            getpbpointer->leftbar = (gpointer)progbar1left;
            getpbpointer->rightbar = (gpointer)progbar1right;
            guint watch_id = gst_bus_add_watch (bus, message_handler, (gpointer)getpbpointer);
            gst_bus_add_watch(bus, bus_call, loop2);
            gst_object_unref(bus);

            gst_element_set_state(pline, GST_STATE_PLAYING);
            gst_element_set_state(pline2, GST_STATE_PLAYING);
            g_main_loop_run(loop2);
            g_source_remove (watch_id);
            g_main_loop_unref (loop2);
        }
        else {
            gst_element_set_state(pline,GST_STATE_NULL);
            gst_element_set_state(pline2,GST_STATE_NULL);
            progbar1left->setValue(progbar1left->minimum());
            progbar1right->setValue(progbar1right->minimum());
            delete getpbpointer;
            g_main_loop_quit(loop2);
        }
        // old capture via v4l2 api
        /*
        static const struct {
            __u32 v4l2_pixfmt;
            QImage::Format qt_pixfmt;
        } supported_fmts[] = {
    #if Q_BYTE_ORDER == Q_BIG_ENDIAN
            { V4L2_PIX_FMT_RGB32, QImage::Format_RGB32 },
            { V4L2_PIX_FMT_RGB24, QImage::Format_RGB888 },
            { V4L2_PIX_FMT_RGB565X, QImage::Format_RGB16 },
            { V4L2_PIX_FMT_RGB555X, QImage::Format_RGB555 },
    #else
            { V4L2_PIX_FMT_BGR32, QImage::Format_RGB32 },
            { V4L2_PIX_FMT_RGB24, QImage::Format_RGB888 },
            { V4L2_PIX_FMT_RGB565, QImage::Format_RGB16 },
            { V4L2_PIX_FMT_RGB555, QImage::Format_RGB555 },
            { V4L2_PIX_FMT_RGB444, QImage::Format_RGB444 },
    #endif
            { 0, QImage::Format_Invalid }
        };
        QImage::Format dstFmt = QImage::Format_RGB888;
        struct v4l2_fract interval;
        v4l2_pix_format &srcPix = m_capSrcFormat.fmt.pix;
        v4l2_pix_format &dstPix = m_capDestFormat.fmt.pix;

        if (!start) {
            stopCapture();
            delete m_capNotifier;
            m_capNotifier = NULL;
            delete m_capImage;
            m_capImage = NULL;
            return;
        }
        m_showFrames = m_showFramesAct->isChecked();
        m_frame = m_lastFrame = m_fps = 0;
        m_capMethod = m_genTab->capMethod();

        if (m_genTab->isSlicedVbi()) {
            v4l2_format fmt;
            v4l2_std_id std;

            m_showFrames = false;
            g_fmt_sliced_vbi(fmt);
            g_std(std);
            fmt.fmt.sliced.service_set = (std & V4L2_STD_625_50) ?
                V4L2_SLICED_VBI_625 : V4L2_SLICED_VBI_525;
            s_fmt(fmt);
            memset(&m_vbiHandle, 0, sizeof(m_vbiHandle));
            m_vbiTab->slicedFormat(fmt.fmt.sliced);
            m_vbiSize = fmt.fmt.sliced.io_size;
            m_frameData = new unsigned char[m_vbiSize];
            if (startCapture(m_vbiSize)) {
                m_capNotifier = new QSocketNotifier(fd(), QSocketNotifier::Read, m_tabs);
                connect(m_capNotifier, SIGNAL(activated(int)), this, SLOT(capVbiFrame()));
            }
            return;
        }
        if (m_genTab->isVbi()) {
            v4l2_format fmt;
            v4l2_std_id std;

            g_fmt_vbi(fmt);
            if (fmt.fmt.vbi.sample_format != V4L2_PIX_FMT_GREY) {
                error("non-grey pixelformat not supported for VBI\n");
                return;
            }
            s_fmt(fmt);
            g_std(std);
            if (!vbi_prepare(&m_vbiHandle, &fmt.fmt.vbi, std)) {
                error("no services possible\n");
                return;
            }
            m_vbiTab->rawFormat(fmt.fmt.vbi);
            m_vbiWidth = fmt.fmt.vbi.samples_per_line;
            if (fmt.fmt.vbi.flags & V4L2_VBI_INTERLACED)
                m_vbiHeight = fmt.fmt.vbi.count[0];
            else
                m_vbiHeight = fmt.fmt.vbi.count[0] + fmt.fmt.vbi.count[1];
            m_vbiSize = m_vbiWidth * m_vbiHeight;
            m_frameData = new unsigned char[m_vbiSize];
            if (m_showFrames) {
                m_capture->setMinimumSize(m_vbiWidth, m_vbiHeight);
                m_capImage = new QImage(m_vbiWidth, m_vbiHeight, dstFmt);
                m_capImage->fill(0);
                m_capture->setImage(*m_capImage, "No frame");
                m_capture->show();
            }
            statusBar()->showMessage("No frame");
            if (startCapture(m_vbiSize)) {
                m_capNotifier = new QSocketNotifier(fd(), QSocketNotifier::Read, m_tabs);
                connect(m_capNotifier, SIGNAL(activated(int)), this, SLOT(capVbiFrame()));
            }
            return;
        }

        g_fmt_cap(m_capSrcFormat);
        s_fmt(m_capSrcFormat);
        if (m_genTab->get_interval(interval))
            set_interval(interval);

        m_mustConvert = m_showFrames;
        m_frameData = new unsigned char[srcPix.sizeimage];
        if (m_showFrames) {
            m_capDestFormat = m_capSrcFormat;
            dstPix.pixelformat = V4L2_PIX_FMT_RGB24;

            for (int i = 0; supported_fmts[i].v4l2_pixfmt; i++) {
                if (supported_fmts[i].v4l2_pixfmt == srcPix.pixelformat) {
                    dstPix.pixelformat = supported_fmts[i].v4l2_pixfmt;
                    dstFmt = supported_fmts[i].qt_pixfmt;
                    m_mustConvert = false;
                    break;
                }
            }
            if (m_mustConvert) {
                v4l2_format copy = m_capSrcFormat;

                // check this line 2017
                v4lconvert_try_format(m_convertData, &m_capDestFormat, &m_capSrcFormat);
                // v4lconvert_try_format sometimes modifies the source format if it thinks
                // that there is a better format available. Restore our selected source
                // format since we do not want that happening.
                m_capSrcFormat = copy;
            }

            m_capture->setMinimumSize(dstPix.width, dstPix.height);
            m_capImage = new QImage(dstPix.width, dstPix.height, dstFmt);
            m_capImage->fill(0);
            m_capture->setImage(*m_capImage, "No frame");
            m_capture->show();
        }

        statusBar()->showMessage("No frame");
        if (startCapture(srcPix.sizeimage)) {
            m_capNotifier = new QSocketNotifier(fd(), QSocketNotifier::Read, m_tabs);
            connect(m_capNotifier, SIGNAL(activated(int)), this, SLOT(capFrame()));
        }
        */

    }
    else if (tab == 2) {
        // listen to the radio without writing an audio file

        if (start)
        {

            loop2 = g_main_loop_new(NULL,FALSE);
            pline2 = gst_pipeline_new("tuneraudioplay");
            pline = gst_pipeline_new("tuneraudiosetfreq");
            alsasrc = gst_element_factory_make("alsasrc","alsasrc");
            rqueue = gst_element_factory_make("queue","queue");
            audioconvert = gst_element_factory_make("audioconvert","audioconvert");

            level = gst_element_factory_make("level","level");
            g_assert (level);
            alsasink = gst_element_factory_make("alsasink","alsasink");

            v4l2radio = gst_element_factory_make("v4l2radio","v4l2radio");
            g_object_set(G_OBJECT(v4l2radio), "device", "/dev/radio0", NULL);

            // get frequency
            double curfreq = linefreq->text().toDouble();
            guint sfreq = curfreq*1000*1000;
            if (sfreq == 0)
            {
                sfreq = 97.2*1000*1000;
            }
            if (sfreq > 108000000) {
                sfreq = 107100000;
            }

            g_object_set(G_OBJECT(v4l2radio), "frequency", sfreq, NULL);
            g_object_set(G_OBJECT(alsasrc), "device","hw:1,0",NULL);
            g_object_set(G_OBJECT(level), "post-messages", TRUE, NULL);



            gst_bin_add_many(GST_BIN(pline2),alsasrc,audioconvert,level,alsasink,NULL);

            gst_element_link_many(alsasrc, audioconvert,level,alsasink,NULL);

            GstCaps *caps;
            caps = gst_caps_from_string("audio/x-raw,format=S16LE,rate=44100,channels=1");

            gboolean link_ok;
            link_ok = gst_element_link_filtered (alsasrc, audioconvert, caps);

            bus = gst_element_get_bus(pline2);
            getpbpointer = new GetProgBarPointer();
            getpbpointer->leftbar = (gpointer)progbar2left;
            getpbpointer->rightbar = (gpointer)progbar2right;

            gst_bin_add(GST_BIN(pline),v4l2radio);
            //gst_element_link(pline,pline2);

            guint watch_id;
            watch_id = gst_bus_add_watch(bus, message_handler, getpbpointer);
            gst_bus_add_watch(bus, bus_call, loop2);
            gst_object_unref(bus);
            gst_element_set_state(pline, GST_STATE_PLAYING);
            gst_element_set_state(pline2, GST_STATE_PLAYING);

            g_main_loop_run(loop2);
            g_main_loop_unref (loop2);

        }
        else {
            gst_element_send_event(pline2,gst_event_new_eos());
            gst_element_set_state(pline,GST_STATE_NULL);
            gst_element_set_state(pline2,GST_STATE_NULL);
            progbar2left->setValue(progbar2left->minimum());
            progbar2right->setValue(progbar2right->minimum());
            delete getpbpointer;
            g_main_loop_quit(loop2);
        }
    }
}

void ApplicationWindow::closeDevice()
{
    delete m_sigMapper;
    m_sigMapper = NULL;
    m_capStartAct->setEnabled(false);
    m_capStartAct->setChecked(false);
    if (fd() >= 0) {
        if (m_capNotifier) {
            delete m_capNotifier;
            delete m_capImage;
            m_capNotifier = NULL;
            m_capImage = NULL;
        }
        delete m_frameData;
        m_frameData = NULL;
        v4lconvert_destroy(m_convertData);
        v4l2::close();
        delete m_capture;
        m_capture = NULL;
    }
    while (QWidget *page = m_tabs->widget(0)) {
        m_tabs->removeTab(0);
        delete page;
    }
    m_ctrlMap.clear();
    m_widgetMap.clear();
    m_classMap.clear();
}

bool SaveDialog::setBuffer(unsigned char *buf, unsigned size)
{
    m_buf = new unsigned char[size];
    m_size = size;
    if (m_buf == NULL)
        return false;
    memcpy(m_buf, buf, size);
    return true;
}

void SaveDialog::selected(const QString &s)
{
    if (!s.isEmpty()) {
        tmpImage->save(s + ".jpg","JPG");
        //file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        //file.write((const char *)m_buf, m_size);
        //file.close();
    }
    delete [] m_buf;
}

void ApplicationWindow::makeSnapshot(unsigned char *buf, unsigned size)
{
    m_makeSnapshot = false;
    SaveDialog *dlg = new SaveDialog(this, "Save Snapshot");
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFileMode(QFileDialog::AnyFile);
    dlg->setAcceptMode(QFileDialog::AcceptSave);
    dlg->setModal(false);
    if (!dlg->setBuffer(buf, size)) {
        delete dlg;
        error("No memory to make snapshot\n");
        return;
    }
    dlg->tmpImage =  m_capImage; // this pointer is a workaround
    connect(dlg, SIGNAL(fileSelected(const QString &)), dlg, SLOT(selected(const QString &)));
    dlg->show();
}

void ApplicationWindow::snapshot()
{
    m_makeSnapshot = true;
}

void ApplicationWindow::rejectedRawFile()
{
    m_saveRawAct->setChecked(false);
}

void ApplicationWindow::openRawFile(const QString &s)
{
    if (s.isEmpty())
        return;

    if (m_saveRaw.openMode()) {

        m_saveRaw.close();
    }
    m_saveRaw.setFileName(s);
    m_saveRaw.open(QIODevice::WriteOnly | QIODevice::Truncate);

    m_saveRawAct->setChecked(true);
}

void ApplicationWindow::saveRaw(bool checked)
{
    if (!checked) {
        if (m_saveRaw.openMode())
            m_saveRaw.close();
        return;
    }

    SaveDialog *dlg = new SaveDialog(this, "Save Raw Frames");
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFileMode(QFileDialog::AnyFile);
    dlg->setAcceptMode(QFileDialog::AcceptSave);
    dlg->setModal(false);
    connect(dlg, SIGNAL(fileSelected(const QString &)), this, SLOT(openRawFile(const QString &)));
    connect(dlg, SIGNAL(rejected()), this, SLOT(rejectedRawFile()));
    dlg->show();
}

void ApplicationWindow::about()
{
    QMessageBox::about(this, "V4L2 Test Bench",
            "This program allows easy experimenting with video4linux devices.");
}

void ApplicationWindow::error(const QString &error)
{
    statusBar()->showMessage(error, 20000);
    if (!error.isEmpty())
        fprintf(stderr, "%s\n", error.toAscii().data());
}

void ApplicationWindow::error(int err)
{
    error(QString("Error: %1").arg(strerror(err)));
}

void ApplicationWindow::errorCtrl(unsigned id, int err)
{
    error(QString("Error %1: %2")
        .arg((const char *)m_ctrlMap[id].name).arg(strerror(err)));
}

void ApplicationWindow::errorCtrl(unsigned id, int err, const QString &v)
{
    error(QString("Error %1 (%2): %3")
        .arg((const char *)m_ctrlMap[id].name).arg(v).arg(strerror(err)));
}

void ApplicationWindow::errorCtrl(unsigned id, int err, long long v)
{
    error(QString("Error %1 (%2): %3")
        .arg((const char *)m_ctrlMap[id].name).arg(v).arg(strerror(err)));
}

void ApplicationWindow::info(const QString &info)
{
    statusBar()->showMessage(info, 5000);
}

void ApplicationWindow::closeEvent(QCloseEvent *event)
{
    closeDevice();
    delete m_capture;
    event->accept();
}

ApplicationWindow *g_mw;

int main(int argc, char **argv)
{
    QApplication a(argc, argv);
    QString device = "/dev/video0";
    bool raw = false;
    bool help = false;
    int i;

    a.setWindowIcon(QIcon(":/qv4l2.png"));
    g_mw = new ApplicationWindow();
    // new in 2017 rename app title
    g_mw->setWindowTitle("KPPC TV&FM on-air broadcasting monitor");
    for (i = 1; i < argc; i++) {
        const char *arg = a.argv()[i];

        if (!strcmp(arg, "-r"))
            raw = true;
        else if (!strcmp(arg, "-h"))
            help = true;
        else if (arg[0] != '-')
            device = arg;
    }
    if (help) {
        printf("qv4l2 [-r] [-h] [device node]\n\n"
               "-h\tthis help message\n"
               "-r\topen device node in raw mode\n");
        return 0;
    }
    g_mw->setDevice(device, raw);
    g_mw->show();
    a.connect(&a, SIGNAL(lastWindowClosed()), &a, SLOT(quit()));
    return a.exec();
}
