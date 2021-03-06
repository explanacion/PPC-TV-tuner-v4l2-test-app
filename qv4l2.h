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

#ifndef QV4L2_H
#define QV4L2_H

#include <QMainWindow>
#include <QTabWidget>
#include <QSignalMapper>
#include <QLabel>
#include <QGridLayout>
#include <QSocketNotifier>
#include <QImage>
#include <QFileDialog>
#include <map>
#include <vector>
#include <QTableWidget>
#include <QProgressBar>

#include "v4l2-api.h"
#include "raw2sliced.h"

// gstreamer
#include <gst/gst.h>
#include <glib.h>

class QComboBox;
class QSpinBox;
class GeneralTab;
class VbiTab;
class QCloseEvent;
class CaptureWin;

typedef std::vector<unsigned> ClassIDVec;
typedef std::map<unsigned, ClassIDVec> ClassMap;
typedef std::map<unsigned, struct v4l2_queryctrl> CtrlMap;
typedef std::map<unsigned, QWidget *> WidgetMap;

enum {
    CTRL_UPDATE_ON_CHANGE = 0x10,
    CTRL_DEFAULTS,
    CTRL_REFRESH,
    CTRL_UPDATE
};

enum CapMethod {
    methodRead,
    methodMmap,
    methodUser
};

struct buffer {
    void   *start;
    size_t  length;
};

class GetProgBarPointer {
public:
    gpointer leftbar;
    gpointer rightbar;
};

class ApplicationWindow: public QMainWindow, public v4l2
{
    Q_OBJECT

public:
    ApplicationWindow();
    virtual ~ApplicationWindow();

private slots:
    void closeDevice();
    void closeCaptureWin();
    void setradiofreq(int, int);
    void setrowradiofreq(int);

public:
    void setDevice(const QString &device, bool rawOpen);
    GetProgBarPointer *getpbpointer;
    // capturing
private:
    CaptureWin *m_capture;

    QLineEdit *linefreq;
    // gstreamer
    // try to start with video only
    GstElement *pline;
    GstElement *v4l2src;
    GstElement *videoqueue;
    GstElement *videoconvert;
    GstElement *theoraenc;
    GstElement *alsasrc;
    GstElement *audioqueue;
    GstElement *audioconvert;
    GstElement *videorate;
    GstElement *vorbisenc;
    GstElement *oggmux;
    GstElement *filesink;
    GMainLoop *loop;
    GstBus *bus;

    GstElement *v4l2radio;
    GstElement *wavenc;
    GstElement *audioresample;
    GstElement *pline2;
    GMainLoop *loop2;
    GstElement *level;
    GstElement *rqueue;
    GstElement *alsasink;

    GstElement *xvimagesink;

    bool startCapture(unsigned buffer_size);
    void stopCapture();
    void stopCapture2();
    void startOutput(unsigned buffer_size);
    void stopOutput();
    struct buffer *m_buffers;
    struct v4l2_format m_capSrcFormat;
    struct v4l2_format m_capDestFormat;
    unsigned char *m_frameData;
    unsigned m_nbuffers;
    struct v4lconvert_data *m_convertData;
    bool m_mustConvert;
    CapMethod m_capMethod;
    bool m_makeSnapshot;

private slots:
    void capStart(bool);
    void capFrame();
    void snapshot();
    void capVbiFrame();
    void saveRaw(bool);
    // gstreamer
    void capStart2(bool);

    // gui
private slots:
    void opendev();
    void openrawdev();
    void ctrlAction(int);
    void openRawFile(const QString &s);
    void rejectedRawFile();

    void about();
    // new in 2017 tabchanged
    void tabchanged();

public:
    virtual void error(const QString &text);
    void error(int err);
    void errorCtrl(unsigned id, int err);
    void errorCtrl(unsigned id, int err, long long v);
    void errorCtrl(unsigned id, int err, const QString &v);
    void info(const QString &info);
    virtual void closeEvent(QCloseEvent *event);

    QProgressBar *progbar1left;
    QProgressBar *progbar1right;

private:
    void addWidget(QGridLayout *grid, QWidget *w, Qt::Alignment align = Qt::AlignLeft);
    void addLabel(QGridLayout *grid, const QString &text, Qt::Alignment align = Qt::AlignRight)
    {
        addWidget(grid, new QLabel(text, parentWidget()), align);
    }
    void addTabs();
    void finishGrid(QGridLayout *grid, unsigned ctrl_class);
    void addCtrl(QGridLayout *grid, const struct v4l2_queryctrl &qctrl);
    void updateCtrl(unsigned id);
    void refresh(unsigned ctrl_class);
    void refresh();
    void makeSnapshot(unsigned char *buf, unsigned size);
    void setDefaults(unsigned ctrl_class);
    int getVal(unsigned id);
    long long getVal64(unsigned id);
    QString getString(unsigned id);
    void setVal(unsigned id, int v);
    void setVal64(unsigned id, long long v);
    void setString(unsigned id, const QString &v);
    QString getCtrlFlags(unsigned flags);
    void setWhat(QWidget *w, unsigned id, const QString &v);
    void setWhat(QWidget *w, unsigned id, long long v);
    void updateVideoInput();
    void updateVideoOutput();
    void updateAudioInput();
    void updateAudioOutput();
    void updateStandard();
    void updateFreq();
    void updateFreqChannel();

    GeneralTab *m_genTab;
    VbiTab *m_vbiTab;
    QAction *m_capStartAct;
    QAction *m_snapshotAct;
    QAction *m_saveRawAct;
    QAction *m_showFramesAct;
    QString m_filename;
    QSignalMapper *m_sigMapper;
    QTabWidget *m_tabs;
    QSocketNotifier *m_capNotifier;
    QImage *m_capImage;
    int m_row, m_col, m_cols;
    CtrlMap m_ctrlMap;
    WidgetMap m_widgetMap;
    ClassMap m_classMap;
    bool m_haveExtendedUserCtrls;
    bool m_showFrames;
    int m_vbiSize;
    unsigned m_vbiWidth;
    unsigned m_vbiHeight;
    struct vbi_handle m_vbiHandle;
    unsigned m_frame;
    unsigned m_lastFrame;
    unsigned m_fps;
    struct timeval m_tv;
    QFile m_saveRaw;
    QTabWidget *r_tabs;
    QTableWidget *radiofreqtable;
    QLabel *proglabel;
    QProgressBar *progbar2left;
    QProgressBar *progbar2right;
};

extern ApplicationWindow *g_mw;

class SaveDialog : public QFileDialog
{
    Q_OBJECT

public:
    QImage *tmpImage;
    SaveDialog(QWidget *parent, const QString &caption) :
        QFileDialog(parent, caption), m_buf(NULL) {}
    virtual ~SaveDialog() {}
    bool setBuffer(unsigned char *buf, unsigned size);

public slots:
    void selected(const QString &s);

private:
    unsigned char *m_buf;
    unsigned m_size;
};

#endif
