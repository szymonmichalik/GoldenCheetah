/*
 * Copyright (c) 2015 Vianney Boyer (vlcvboyer@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <QtGui>
#include <QGraphicsPathItem>
#include "MeterWidget.h"
#include "ErgFile.h"
#include "Context.h"
#include "Units.h"

#include <QtWebEngineWidgets/QWebEngineView>

MeterWidget::MeterWidget(QString Name, QWidget *parent, QString Source) : QWidget(parent), m_Name(Name), m_container(parent), m_Source(Source)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

#ifdef Q_OS_LINUX
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
#else
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_PaintOnScreen);
#endif

    setAttribute(Qt::WA_TransparentForMouseEvents);

    //default settings
    m_MainColor = QColor(255,0,0,180);
    m_ScaleColor = QColor(200,200,200,200);
    m_OutlineColor = QColor(128,128,128,180);
    m_MainFont = QFont(this->font().family(), 64);
    m_AltFont = QFont(this->font().family(), 48);
    m_BackgroundColor = QColor(96, 96, 96, 0);
    m_RangeMin = 0;
    m_RangeMax = 100;
    m_Angle = 180.0;
    m_SubRange = 10;
    m_Zoom = 16;
    boundingRectVisibility = false;
    backgroundVisibility = false;
    forceSquareRatio = true;
}

void MeterWidget::SetRelativeSize(float RelativeWidth, float RelativeHeight)
{
    m_RelativeWidth = RelativeWidth / 100.0;
    m_RelativeHeight = RelativeHeight / 100.0;
    AdjustSizePos();
}

void MeterWidget::SetRelativePos(float RelativePosX, float RelativePosY)
{
    m_RelativePosX = RelativePosX / 100.0;
    m_RelativePosY = RelativePosY / 100.0;
    AdjustSizePos();
}

void MeterWidget::AdjustSizePos()
{
    // Compute the size and position relative to its parent
    QPoint p;
    if (m_container->windowFlags() & Qt::Window)
        p = m_container->pos();
    else
        p = m_container->mapToGlobal(m_container->pos());
    ComputeSize();
    m_PosX = p.x() + m_container->width() * m_RelativePosX - m_Width/2;
    m_PosY = p.y() + m_container->height() * m_RelativePosY - m_Height/2;
    move(m_PosX, m_PosY);
    adjustSize();

    // Translate the Video Container visible region to our coordinate for clipping
    QPoint vp = m_VideoContainer->pos();
    videoContainerRegion = m_VideoContainer->visibleRegion();
    videoContainerRegion.translate(mapFromGlobal(m_VideoContainer->mapToGlobal(vp)) - vp);
}

void MeterWidget::ComputeSize()
{
    if (forceSquareRatio)
    {
        m_Width = m_Height = (m_container->width() * m_RelativeWidth + m_container->height() * m_RelativeHeight) / 2;
    }
    else
    {
        m_Width = m_container->width() * m_RelativeWidth;
        m_Height =  m_container->height() * m_RelativeHeight;
    }
}

QSize MeterWidget::sizeHint() const
{
    return QSize(m_Width, m_Height);
}

QSize MeterWidget::minimumSize() const
{
    return QSize(m_Width, m_Height);
}

void MeterWidget::paintEvent(QPaintEvent* paintevent)
{
    Q_UNUSED(paintevent);
    if(!boundingRectVisibility && !backgroundVisibility) return;

    QPainter painter(this);
    painter.setClipRegion(videoContainerRegion);
    painter.setRenderHint(QPainter::Antialiasing);

    int radius = qMin(m_Width, m_Height) * 0.1;
    if(backgroundVisibility) painter.setBrush(QBrush(m_BackgroundColor));
    else painter.setBrush(Qt::NoBrush);

    if (boundingRectVisibility)
    {
        m_OutlinePen = QPen(m_BoundingRectColor);
        m_OutlinePen.setWidth(2);
        m_OutlinePen.setStyle(Qt::SolidLine);

        painter.setPen(m_OutlinePen);
        painter.drawRoundedRect (1, 1, m_Width-2, m_Height-2, radius, radius);
    }
    else
    {
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect (0, 0, m_Width, m_Height, radius, radius);
    }
}

void  MeterWidget::setColor(QColor  mainColor)
{
    m_MainColor = mainColor;
}

void MeterWidget::setBoundingRectVisibility(bool show, QColor  boundingRectColor)
{
    this->boundingRectVisibility=show;
    this->m_BoundingRectColor = boundingRectColor;
}

TextMeterWidget::TextMeterWidget(QString Name, QWidget *parent, QString Source) : MeterWidget(Name, parent, Source)
{
    forceSquareRatio = false;
}

void TextMeterWidget::paintEvent(QPaintEvent* paintevent)
{
    MeterWidget::paintEvent(paintevent);

    m_MainBrush = QBrush(m_MainColor);
    m_OutlinePen = QPen(m_OutlineColor);
    m_OutlinePen.setWidth(1);
    m_OutlinePen.setStyle(Qt::SolidLine);

    //painter
    QPainter painter(this);
    painter.setClipRegion(videoContainerRegion);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath my_painterPath;
    my_painterPath.addText(QPointF(0,0),m_MainFont,Text);
    my_painterPath.addText(QPointF(QFontMetrics(m_MainFont).width(Text), 0),m_AltFont,AltText);
    QRectF ValueBoundingRct = my_painterPath.boundingRect();

    // We use leading whitespace for alignment which boundingRect() does not count
    ValueBoundingRct.setLeft(qMin(0.0, ValueBoundingRct.left()));

    // The scale should not change with the string content, we use Font ascent and descent
    ValueBoundingRct.setTop(qMin(ValueBoundingRct.top(), qreal(qMin(-QFontMetrics(m_MainFont).ascent(), 
            -QFontMetrics(m_AltFont).ascent()))));
    ValueBoundingRct.setBottom(qMax(ValueBoundingRct.bottom(), qreal(qMax(QFontMetrics(m_MainFont).descent(),
            QFontMetrics(m_AltFont).descent()))));

    // scale to fit the available space
    float fontscale = qMin(m_Width / ValueBoundingRct.width(), m_Height / ValueBoundingRct.height());
    painter.scale(fontscale, fontscale);

    float translationX = -ValueBoundingRct.x();  // AlignLeft

    if(alignment == Qt::AlignHCenter)
        translationX += (m_Width/fontscale - ValueBoundingRct.width())/2;
    else if(alignment == Qt::AlignRight)
        translationX += (m_Width/fontscale - ValueBoundingRct.width());

    painter.translate(translationX, -ValueBoundingRct.y()+(m_Height/fontscale - ValueBoundingRct.height())/2);

    // Write Value
    painter.setPen(m_OutlinePen);
    painter.setBrush(m_MainBrush);
    painter.drawPath(my_painterPath);
}

CircularIndicatorMeterWidget::CircularIndicatorMeterWidget(QString Name, QWidget *parent, QString Source) : MeterWidget(Name, parent, Source)
{
    //defaut settings
    IndicatorGradient = QConicalGradient(0, 0, 150);
    IndicatorGradient.setColorAt(0.0, QColor(255,0,0,180));
    IndicatorGradient.setColorAt(0.3, QColor(255,255,0,180));
    IndicatorGradient.setColorAt(0.7, QColor(0,255,0,180));
    IndicatorGradient.setColorAt(1.0, QColor(0,255,0,180));
}

void CircularIndicatorMeterWidget::paintEvent(QPaintEvent* paintevent)
{
    MeterWidget::paintEvent(paintevent);

    m_MainBrush = QBrush(m_MainColor);
    m_OutlinePen = QPen(m_OutlineColor);
    m_OutlinePen.setWidth(1);
    m_OutlinePen.setStyle(Qt::SolidLine);

    //painter
    QPainter painter(this);
    painter.setClipRegion(videoContainerRegion);
    painter.setRenderHint(QPainter::Antialiasing);
    // define scale and location
    painter.translate(m_Width / 2, m_Height / 2);
    painter.scale(m_Width / 200.0, m_Height / 200.0);

    // Draw circular indicator
    // Define coordinates:
    static const QPoint CP_pt1(0, -70);
    static const QPoint CP_pt2(0, -90);
    static const QRectF CP_extRect(-90,-90,180,180);
    static const QRectF CP_intRect(-70,-70,140,140);
    // rotate painter
    painter.save();
    painter.rotate(-m_Angle/2);

    double CPAngle = qBound((float) -1.0, (Value-m_RangeMin) / (m_RangeMax-m_RangeMin), (float) 1.0) * m_Angle;
    QPainterPath CPEmptyPath;
    CPEmptyPath.moveTo(CP_pt1);
    CPEmptyPath.arcMoveTo(CP_intRect, 90 - CPAngle);
    QPainterPath CPIndicatorPath;
    CPIndicatorPath.moveTo(CP_pt1);
    CPIndicatorPath.lineTo(CP_pt2);
    CPIndicatorPath.arcTo(CP_extRect, 90, -1 * CPAngle);
    CPIndicatorPath.lineTo(CPEmptyPath.currentPosition());
    CPIndicatorPath.arcTo(CP_intRect, 90 - CPAngle, CPAngle);
    painter.setBrush(IndicatorGradient);
    painter.setPen(Qt::NoPen);
    painter.drawPath(CPIndicatorPath);
    painter.restore();
}

CircularBargraphMeterWidget::CircularBargraphMeterWidget(QString Name, QWidget *parent, QString Source) : MeterWidget(Name, parent, Source)
{
}

void CircularBargraphMeterWidget::paintEvent(QPaintEvent* paintevent)
{
    MeterWidget::paintEvent(paintevent);

    m_MainBrush = QBrush(m_MainColor);
    m_OutlinePen = QPen(m_MainColor);
    m_OutlinePen.setWidth(3);
    m_OutlinePen.setStyle(Qt::SolidLine);

    //painter
    QPainter painter(this);
    painter.setClipRegion(videoContainerRegion);
    painter.setRenderHint(QPainter::Antialiasing);

    //draw bargraph
    painter.setPen(m_OutlinePen);
    painter.setBrush(m_MainBrush);
    painter.save();
    painter.translate(m_Width / 2, m_Height / 2);
    painter.rotate((360.0-m_Angle)/2.0);
    for (int i=0; i<=(int)(m_SubRange*qMin(1.0,(double)(Value-m_RangeMin)/(m_RangeMax-m_RangeMin))); i++)
    {
        painter.drawLine (0, m_Height*2/10, 0, m_Height*4/10);
        painter.rotate(m_Angle/m_SubRange);
    }
    painter.restore();
}

NeedleMeterWidget::NeedleMeterWidget(QString Name, QWidget *parent, QString Source) : MeterWidget(Name, parent, Source)
{
}

void NeedleMeterWidget::paintEvent(QPaintEvent* paintevent)
{
    MeterWidget::paintEvent(paintevent);

    m_MainBrush = QBrush(m_MainColor);
    m_BackgroundBrush = QBrush(m_BackgroundColor);
    m_OutlinePen = QPen(m_OutlineColor);
    m_OutlinePen.setWidth(1);
    m_OutlinePen.setStyle(Qt::SolidLine);
    m_ScalePen = QPen(m_ScaleColor);
    m_ScalePen.setWidth(2);
    m_ScalePen.setStyle(Qt::SolidLine);

    //painter
    QPainter painter(this);
    painter.setClipRegion(videoContainerRegion);
    painter.setRenderHint(QPainter::Antialiasing);

    //draw background
    painter.setPen(m_OutlinePen);
    painter.setBrush(m_BackgroundBrush);
    painter.drawEllipse (0, 0, m_Width, m_Height);

    //draw scale
    painter.setPen(m_ScalePen);
    painter.setBrush(Qt::NoBrush);
    painter.save();
    painter.translate(m_Width / 2, m_Height / 2);
    painter.rotate(m_Angle/2.0-90.0);
    painter.drawArc (-m_Width*4/10, -m_Height*4/10, m_Width*8/10, m_Height*8/10, 0, (int) (16.0*m_Angle));
    painter.restore();
    painter.save();
    painter.translate(m_Width / 2, m_Height / 2);
    painter.rotate((360.0-m_Angle)/2.0);
    for (int i=0; i<=m_SubRange; i++)
    {
        painter.drawLine (0, m_Height*3/10, 0, m_Height*4/10);
        painter.rotate(m_Angle/m_SubRange);
    }
    painter.restore();

    //draw needle
    painter.setPen(m_OutlinePen);
    painter.setBrush(m_MainBrush);
    QPainterPath my_painterPath;
    painter.save();
    painter.translate(m_Width / 2, m_Height / 2);
    painter.rotate(-m_Angle/2+(qBound((float) -1.0, (Value-m_RangeMin)/(m_RangeMax-m_RangeMin), (float) 1.0)*m_Angle));
    my_painterPath.moveTo(-2, 0);
    my_painterPath.lineTo(0, -m_Height*4/10);
    my_painterPath.lineTo(+2, 0);
    my_painterPath.lineTo(-2, 0);
    painter.drawPath(my_painterPath);
    painter.restore();
}

ElevationMeterWidget::ElevationMeterWidget(QString Name, QWidget *parent, QString Source, Context *context) : MeterWidget(Name, parent, Source), context(context),
    m_minX(0.), m_maxX(0.), m_savedWidth(0), m_savedHeight(0), gradientValue(0.)
{
    forceSquareRatio = false;
}

// Compute polygon for elevation graph based on widget size.
// This is two full scans of the ergfile point array.
void ElevationMeterWidget::lazyDimensionCompute(void)
{
    // Nothing to compute unless there is an erg file.
    if (!context || !context->currentErgFile())
        return;

    // Compute if size has changed
    if (m_savedWidth != m_Width || m_savedHeight != m_Height) {

        // Determine extents of route
        double minX, minY, maxX, maxY;
        minX = maxX = context->currentErgFile()->Points[0].x; // meters
        minY = maxY = context->currentErgFile()->Points[0].y; // meters or altitude???
        foreach(ErgFilePoint x, context->currentErgFile()->Points) {
            minX = std::min(minX, x.x);
            minY = std::min(minY, x.y);
            maxX = std::max(maxX, x.x);
            maxY = std::max(maxY, x.y);
        }

        if (m_Width != 0 && (maxY - minY) / 0.05 < (double)m_Height * 0.80 * (maxX - minX) / (double)m_Width)
            maxY = minY + (double)m_Height * 0.80 * (maxX - minX) / (double)m_Width * 0.05;
        minY -= (maxY - minY) * 0.20f; // add 20% as bottom headroom (slope gradient will be shown there in a bubble)

        // Populate elevation route polygon
        m_elevationPolygon.clear();
        m_elevationPolygon << QPoint(0.0, (double)m_Height);
        double x = 0, y = 0;
        double nextX = 1;
        for (double pt=0; pt < context->currentErgFile()->Points.size(); pt++) {
            for (; x < nextX && pt < context->currentErgFile()->Points.size(); pt++) {
                x = (context->currentErgFile()->Points[pt].x - minX) * (double)m_Width / (maxX - minX);
                y = (context->currentErgFile()->Points[pt].y - minY) * (double)m_Height / (maxY - minY);
            }
            // Add points to polygon only once every time the x coordinate integer part changes.
            m_elevationPolygon << QPoint(x, (double)m_Height - y);
            nextX = floor(x) + 1.0;
        }
        m_elevationPolygon << QPoint((double)m_Width, (double)m_Height);

        // Save distance extent, used to situate rider location within widget display.
        m_minX = minX;
        m_maxX = maxX;

        m_savedWidth = m_Width;
        m_savedHeight = m_Height;
    }
}

void ElevationMeterWidget::paintEvent(QPaintEvent* paintevent)
{
    // TODO : show Power when not in slope simulation mode
    if (!context || !context->currentErgFile() || context->currentErgFile()->Points.size()<=1)
        return;

    MeterWidget::paintEvent(paintevent);

    m_MainBrush = QBrush(m_MainColor);
    m_BackgroundBrush = QBrush(m_BackgroundColor);
    m_OutlinePen = QPen(m_OutlineColor);
    m_OutlinePen.setWidth(1);
    m_OutlinePen.setStyle(Qt::SolidLine);

    //painter
    QPainter painter(this);
    painter.setClipRegion(videoContainerRegion);
    painter.setRenderHint(QPainter::Antialiasing);

    // Lazy compute of min, max and route elevation polygon on init or if dimensions
    // change. Ideally we'd use the resize event but we are computing from ergfile
    // which isnt available when initial resize occurs.
    lazyDimensionCompute();

    double bubbleSize = (double)m_Height * 0.010f;

    QPolygon polygon;
    polygon << QPoint(fmin((double) m_Width, bubbleSize), (double)m_Height);

    double cyclistX = (this->Value * 1000.0 - m_minX) * (double)m_Width / (m_maxX - m_minX);
    polygon << QPoint(cyclistX, (double)m_Height-bubbleSize);
    polygon << QPoint(fmax(0.0, cyclistX-bubbleSize), (double)m_Height);

    painter.setPen(m_OutlinePen);
    painter.setBrush(m_BackgroundBrush);
    painter.drawPolygon(m_elevationPolygon);
    painter.drawPolygon(polygon);

    m_OutlinePen = QPen(m_MainColor);
    m_OutlinePen.setWidth(1);
    m_OutlinePen.setStyle(Qt::SolidLine);
    painter.setPen(m_OutlinePen);
    painter.drawLine(cyclistX, 0.0, cyclistX, (double)m_Height-bubbleSize);

    // Display grade as #.#%
    QString gradientString = ((-1.0 < this->gradientValue && this->gradientValue < 0.0) ? QString("-") : QString("")) + QString::number((int)this->gradientValue) +
        QString(".") + QString::number(abs((int)(this->gradientValue * 10.0) % 10)) + QString("%");

    // Display gradient text to the right of the line until the middle, then display to the left of the line
    double gradientDrawX = cyclistX;
    double gradientDrawY = m_Height * 0.95;

    if (cyclistX < m_Width * 0.5)
        gradientDrawX += 5.;
    else
        gradientDrawX =- 45.;

    painter.drawText(gradientDrawX, gradientDrawY, gradientString);

    double routeDistance = this->Value;
    if (!context->athlete->useMetricUnits) routeDistance *= MILES_PER_KM;

    routeDistance = ((int)(routeDistance * 1000.)) / 1000.;

    QString distanceString = QString::number(routeDistance, 'f', 3) +
        ((context->athlete->useMetricUnits) ? tr("km") : tr("mi"));

    double distanceDrawX = (double)cyclistX;
    double distanceDrawY = ((double)m_Height * 0.75);

    // Display distance text to the right of the line until the middle, then display to the left of the line
    if (cyclistX < m_Width * 0.5)
        distanceDrawX += 5;
    else
        distanceDrawX -= 45;

    painter.drawText(distanceDrawX, distanceDrawY, distanceString);
}


LiveMapWidget::LiveMapWidget(QString Name, QWidget *parent, QString Source) : MeterWidget(Name, parent, Source)
{
    forceSquareRatio = false;
    liveMapView = new QWebEngineView(this);
    liveMapInitialized = false;
}

void LiveMapWidget::resizeEvent(QResizeEvent *)
{
    liveMapView->resize(m_Width, m_Height);
}

void LiveMapWidget::plotNewLatLng(double dLat, double dLon)
{
    if ( ! liveMapInitialized ) initLiveMap(dLat, dLon);

    QString sLat = QString::number(dLat);
    QString sLon = QString::number(dLon);
    QString code = QString("moveMarker(" + sLat + " , "  + sLon + ")");

    liveMapView->page()->runJavaScript(code);
}

void LiveMapWidget::initLiveMap(double dLat, double dLon)
{
    createHtml(dLat, dLon, m_Zoom);
    liveMapView->page()->setHtml(currentPage);
    liveMapView->show();
    liveMapInitialized = true;
}

void LiveMapWidget::createHtml(double dLat, double dLon, int iMapZoom)
{
    QString sLat = QString::number(dLat);
    QString sLon = QString::number(dLon);
    QString sWidth = QString::number(m_Width);
    QString sHeight = QString::number(m_Height);
    QString sMapZoom = QString::number(iMapZoom);
    currentPage = "";

    currentPage = QString("<html><head>\n"
    "<meta name=\"viewport\" content=\"initial-scale=1.0, user-scalable=yes\"/> \n"
    "<meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\"/>\n"
    "<title>GoldenCheetah LiveMap - TrainView</title>\n");
    //Leaflet CSS and JS
    currentPage += QString("<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.6.0/dist/leaflet.css\"\n"
    "integrity=\"sha512-xwE/Az9zrjBIphAcBb3F6JVqxf46+CDLwfLMHloNu6KEQCAWi6HcDUbeOfBIptF7tcCzusKFjFw2yuvEpDL9wQ==\" crossorigin=\"\"/>\n"
	"<script src=\"https://unpkg.com/leaflet@1.6.0/dist/leaflet.js\"\n"
    "integrity=\"sha512-gZwIG9x3wUXg2hdXF6+rVkLF/0Vi9U8D2Ntg4Ga5I5BZpVkVxlJWbSQtXPSiUTtC0TjtGOmxa1AJPuV0CPthew==\" crossorigin=\"\"></script>\n"
	"<style>#mapid {height:100%; width:100%}</style></head>\n");

    // local functions
    currentPage += QString("<body><div id=\"mapid\"></div>\n"
    "<script type=\"text/javascript\">\n");
    // Create Map options
    currentPage += QString("var mapOptions = {\n"
        "    center: [" + sLat + ", " + sLon + "] ,\n"
        "    zoom : " + sMapZoom + ",\n"
        "    zoomControl : true,\n"
        "    scrollWheelZoom : false,\n"
        "    dragging : false,\n"
        "    doubleClickZoom : false}\n");
    // Create map object
    currentPage += QString("var mymap = L.map('mapid', mapOptions);\n");
    // Create layer object
    currentPage += QString("var layer = new L.tileLayer('http://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png');\n");
    // Add layer to the map
    currentPage += QString("mymap.addLayer(layer);\n");
    // mymarker var
    currentPage += QString("var mymarker = new L.marker([" + sLat + ", " + sLon + "], {\n"
    "    draggable: false,\n"
    "    title: \"GoldenCheetah - Workout LiveMap\",\n"
    "    alt: \"GoldenCheetah - Workout LiveMap\",\n"
    "    riseOnHover: true\n"
    "}).addTo(mymap)\n");
    // Move marker function
    currentPage += QString(    "function moveMarker(myLat, myLon) { \n"
    "   mymap.panTo(new L.LatLng(myLat, myLon));\n"
    "    mymarker.setLatLng(new L.latLng(myLat, myLon));}\n"
    "</script>\n"
    "</body></html>\n");
}
