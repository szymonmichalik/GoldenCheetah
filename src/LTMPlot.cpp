/*
 * Copyright (c) 2010 Mark Liversedge (liversedge@gmail.com)
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

#include "Athlete.h"
#include "Context.h"
#include "LTMPlot.h"
#include "LTMTool.h"
#include "LTMTrend.h"
#include "LTMTrend2.h"
#include "LTMOutliers.h"
#include "LTMWindow.h"
#include "RideMetric.h"
#include "RideCache.h"
#include "RideFileCache.h"
#include "Settings.h"
#include "Colors.h"

#include "PMCData.h" // for LTS/STS calculation
#include "Zones.h"
#include "HrZones.h"
#include "PaceZones.h"

#include <QSettings>

#include <qwt_series_data.h>
#include <qwt_scale_widget.h>
#include <qwt_legend.h>
#include <qwt_legend_label.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_canvas.h>
#include <qwt_curve_fitter.h>
#include <qwt_plot_grid.h>
#include <qwt_symbol.h>

#include <cmath> // for isinf() isnan()

LTMPlot::LTMPlot(LTMWindow *parent, Context *context, bool first) : 
    bg(NULL), parent(parent), context(context), highlighter(NULL), first(first), isolation(false)
{
    // don't do this ..
    setAutoReplot(false);
    setAutoFillBackground(true);

    // set up the models we support
    models << new CP2Model(context);
    models << new CP3Model(context);
    models << new MultiModel(context);
    models << new ExtendedModel(context);

    // setup my axes
    // for now we limit to 4 on left and 4 on right
    setAxesCount(QwtAxis::yLeft, 4);
    setAxesCount(QwtAxis::yRight, 4);
    setAxesCount(QwtAxis::xBottom, 1);
    setAxesCount(QwtAxis::xTop, 0);

    for (int i=0; i<4; i++) {

        // lefts
        QwtAxisId left(QwtAxis::yLeft, i);
        supportedAxes << left;
        
        QwtScaleDraw *sd = new QwtScaleDraw;
        sd->setTickLength(QwtScaleDiv::MajorTick, 3);
        sd->enableComponent(QwtScaleDraw::Ticks, false);
        sd->enableComponent(QwtScaleDraw::Backbone, false);

        setAxisScaleDraw(left, sd);
        setAxisMaxMinor(left, 0);
        setAxisVisible(left, false);

        QwtAxisId right(QwtAxis::yRight, i);
        supportedAxes << right;

        // lefts
        sd = new QwtScaleDraw;
        sd->setTickLength(QwtScaleDiv::MajorTick, 3);
        sd->enableComponent(QwtScaleDraw::Ticks, false);
        sd->enableComponent(QwtScaleDraw::Backbone, false);
        setAxisScaleDraw(right, sd);
        setAxisMaxMinor(right, 0);
        setAxisVisible(right, false);
    }

    // get application settings
    insertLegend(new QwtLegend(), QwtPlot::BottomLegend);
    setAxisTitle(QwtAxis::xBottom, tr("Date"));
    enableAxis(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xTop, false);
    setAxisMaxMinor(QwtPlot::xBottom,-1);
    setAxisScaleDraw(QwtPlot::xBottom, new LTMScaleDraw(QDateTime::currentDateTime(), 0, LTM_DAY));

    static_cast<QwtPlotCanvas*>(canvas())->setFrameStyle(QFrame::NoFrame);

    grid = new QwtPlotGrid();
    grid->enableX(false);
    grid->attach(this);

    // manage our own picker
    picker = new LTMToolTip(QwtPlot::xBottom, QwtPlot::yLeft, QwtPicker::VLineRubberBand, QwtPicker::AlwaysOn, canvas(), "");
    picker->setMousePattern(QwtEventPattern::MouseSelect1, Qt::LeftButton);
    picker->setTrackerPen(QColor(Qt::black));

    QColor inv(Qt::white);
    inv.setAlpha(0);
    picker->setRubberBandPen(inv); // make it invisible
    picker->setEnabled(true);
    _canvasPicker = new LTMCanvasPicker(this);

    curveColors = new CurveColors(this);

    settings = NULL;

    configChanged(CONFIG_APPEARANCE); // set basic colors

    connect(context, SIGNAL(configChanged(qint32)), this, SLOT(configChanged(qint32)));
    // connect pickers to ltmPlot
    connect(_canvasPicker, SIGNAL(pointHover(QwtPlotCurve*, int)), this, SLOT(pointHover(QwtPlotCurve*, int)));
    connect(_canvasPicker, SIGNAL(pointClicked(QwtPlotCurve*, int)), this, SLOT(pointClicked(QwtPlotCurve*, int)));
}

LTMPlot::~LTMPlot()
{
}

void
LTMPlot::configChanged(qint32)
{
    // set basic plot colors
    setCanvasBackground(GColor(CPLOTBACKGROUND));
    QPen gridPen(GColor(CPLOTGRID));
    //gridPen.setStyle(Qt::DotLine);
    grid->setPen(gridPen);

    QPalette palette;
    palette.setBrush(QPalette::Window, QBrush(GColor(CPLOTBACKGROUND)));
    palette.setColor(QPalette::WindowText, GColor(CPLOTMARKER));
    palette.setColor(QPalette::Text, GColor(CPLOTMARKER));
    setPalette(palette);

    QPalette gray = palette; // same but with gray text for hidden curves
    gray.setColor(QPalette::WindowText, Qt::darkGray);
    gray.setColor(QPalette::Text, Qt::darkGray);

    axesObject.clear();
    axesId.clear();
    foreach (QwtAxisId x, supportedAxes) {
        axisWidget(x)->setPalette(palette);
        axisWidget(x)->setPalette(palette);

        // keep track
        axisWidget(x)->removeEventFilter(this);
        axisWidget(x)->installEventFilter(this);
        axesObject << axisWidget(x);
        axesId << x;

    }
    axisWidget(QwtPlot::xBottom)->setPalette(palette);

    QwtLegend *l = static_cast<QwtLegend *>(this->legend());
    foreach(QwtPlotCurve *p, curves) {
        foreach (QWidget *w, l->legendWidgets(itemToInfo(p))) {
            for(int m=0; m< settings->metrics.count(); m++) {
                if (settings->metrics[m].curve == p)
                    if (settings->metrics[m].hidden == false) 
                        w->setPalette(palette);
                    else
                        w->setPalette(gray);
            }
        }
    }

    // now save state
    curveColors->saveState();
    updateLegend();

    if (legend()) legend()->installEventFilter(this);
}

void
LTMPlot::setAxisTitle(QwtAxisId axis, QString label)
{
    // setup the default fonts
    QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
    stGiles.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
    stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

    QwtText title(label);
    title.setFont(stGiles);
    QwtPlot::setAxisFont(axis, stGiles);
    QwtPlot::setAxisTitle(axis, title);
}

void
LTMPlot::setData(LTMSettings *set)
{
    QTime timer;
    timer.start();

    curveColors->isolated = false;
    isolation = false;

    //qDebug()<<"Starting.."<<timer.elapsed();

    settings = set;

    // For each metric in chart, translate units and name if default uname
    //XXX BROKEN XXX LTMTool::translateMetrics(context, settings);

    // crop dates to at least within a year of the data available, but only if we have some data
    if (context->athlete->rideCache->rides().count()) {

        QDateTime first = context->athlete->rideCache->rides().first()->dateTime;
        QDateTime last = context->athlete->rideCache->rides().last()->dateTime;

        // if dates are null we need to set them from the available data

        // end
        if (settings->end == QDateTime() || settings->end > last.addDays(365)) {
            if (settings->end < QDateTime::currentDateTime()) {
                settings->end = QDateTime::currentDateTime();
            } else {
                settings->end = last;
            }
        }

        // start
        if (settings->start == QDateTime() || settings->start < first.addDays(-365)) {
            settings->start = first;
        }
    }

    //setTitle(settings->title);
    if (settings->groupBy != LTM_TOD)
        setAxisTitle(xBottom, tr("Date"));
    else
        setAxisTitle(xBottom, tr("Time of Day"));
    enableAxis(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xTop, false);

    // wipe existing curves/axes details
    QHashIterator<QString, QwtPlotCurve*> c(curves);
    while (c.hasNext()) {
        c.next();
        QString symbol = c.key();
        QwtPlotCurve *current = c.value();
        //current->detach(); // the destructor does this for you
        delete current;
    }
    curves.clear();
    if (highlighter) {
        highlighter->detach();
        delete highlighter;
        highlighter = NULL;
    }
    foreach (QwtPlotMarker *label, labels) {
        label->detach();
        delete label;
    }
    labels.clear();
    // clear old markers - if there are any
    foreach(QwtPlotMarker *m, markers) {
        m->detach();
        delete m;
    }
    markers.clear();



    // disable all y axes until we have populated
    for (int i=0; i<8; i++) {
        setAxisVisible(supportedAxes[i], false);
        enableAxis(supportedAxes[i].id, false);
    }
    axes.clear();
    axesObject.clear();
    axesId.clear();

    // reset all min/max Y values
    for (int i=0; i<10; i++) minY[i]=0, maxY[i]=0;

    // no data to display so that all folks
    if (context->athlete->rideCache->rides().count() == 0) {

        // tidy up the bottom axis
        maxX = groupForDate(settings->end.date(), settings->groupBy) -
                groupForDate(settings->start.date(), settings->groupBy);

        setAxisScale(xBottom, 0, maxX);
        setAxisScaleDraw(QwtPlot::xBottom, new LTMScaleDraw(settings->start,
                groupForDate(settings->start.date(), settings->groupBy), settings->groupBy));
        enableAxis(QwtAxis::xBottom, true);
        setAxisVisible(QwtAxis::xBottom, true);
        setAxisVisible(QwtAxis::xTop, false);

        // remove the shading if it exists
        refreshZoneLabels(QwtAxisId(-1,-1)); // turn em off

        // remove the old markers
        refreshMarkers(settings, settings->start.date(), settings->end.date(), settings->groupBy, GColor(CPLOTMARKER));

        replot();
        return;
    }

    //qDebug()<<"Wiped previous.."<<timer.elapsed();

    // count the bars since we format them side by side and need
    // to now how to offset them from each other
    // unset stacking if not a bar chart too since we don't support
    // that yet, but would be good to add in the future (stacked
    // area plot).
    int barnum=0;
    int bars = 0;
    int stacknum = -1;
    // index through rather than foreach so we can modify
    for (int v=0; v<settings->metrics.count(); v++) {
        if (settings->metrics[v].curveStyle == QwtPlotCurve::Steps) {
            if (settings->metrics[v].stack && stacknum < 0) stacknum = bars++; // starts from 1 not zero
            else if (settings->metrics[v].stack == false) bars++;
        } else if (settings->metrics[v].stack == true)
            settings->metrics[v].stack = false; // we only support stack on bar charts
    }

    // aggregate the stack curves - backwards since
    // we plot forwards overlaying to create the illusion
    // of a stack, when in fact its just bars of descending
    // order (with values aggregated)

    // free stack memory
    foreach(QVector<double>*p, stackX) delete p;
    foreach(QVector<double>*q, stackY) delete q;
    stackX.clear();
    stackY.clear();
    stacks.clear();

    int r=0;
    foreach (MetricDetail metricDetail, settings->metrics) {
        if (metricDetail.stack == true) {

            // register this data
            QVector<double> *xdata = new QVector<double>();
            QVector<double> *ydata = new QVector<double>();
            stackX.append(xdata);
            stackY.append(ydata);

            int count;
            if (settings->groupBy != LTM_TOD)
                createCurveData(context, settings, metricDetail, *xdata, *ydata, count);
            else
                createTODCurveData(context, settings, metricDetail, *xdata, *ydata, count);

            // we add in the last curve for X axis values
            if (r) {
                aggregateCurves(*stackY[r], *stackY[r-1]);
            }
            r++;
        }
    }

    //qDebug()<<"Created curve data.."<<timer.elapsed();

    // setup the curves
    double width = appsettings->value(this, GC_LINEWIDTH, 0.5).toDouble();
    bool donestack = false;

    // now we iterate over the metric details AGAIN
    // but this time in reverse and only plot the
    // stacked values. This is because we overcome the
    // lack of a stacked plot in QWT by painting decreasing
    // bars, with the values aggregated previously
    // so if we plot L1 time in zone 1hr and L2 time in zone 1hr
    // it plots as L2 time in zone 2hr and then paints over that
    // with a L1 time in zone of 1hr.
    //
    // The tooltip has to unpick the aggregation to ensure
    // that it subtracts other data series in the stack from
    // the value plotted... all nasty but heck, it works
    int stackcounter = stackX.size()-1;
    for (int m=settings->metrics.count()-1; m>=0; m--) {

        //
        // *ONLY* PLOT STACKS
        //

        int count=0;
        MetricDetail metricDetail = settings->metrics[m];

        if (metricDetail.stack == false) continue;

        QVector<double> xdata, ydata;

        // use the aggregated values
        xdata = *stackX[stackcounter];
        ydata = *stackY[stackcounter];
        stackcounter--;
        count = xdata.size()-2;

        // no data to plot!
        if (count <= 0) continue;

        // Create a curve
        QwtPlotCurve *current = new QwtPlotCurve(metricDetail.uname);
        current->setVisible(!metricDetail.hidden);
        settings->metrics[m].curve = current;
        if (metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS)
            curves.insert(metricDetail.bestSymbol, current);
        else
            curves.insert(metricDetail.symbol, current);
        stacks.insert(current, stackcounter+1);
        if (appsettings->value(this, GC_ANTIALIAS, true).toBool() == true)
            current->setRenderHint(QwtPlotItem::RenderAntialiased);
        QPen cpen = QPen(metricDetail.penColor);
        cpen.setWidth(width);
        current->setPen(cpen);
        current->setStyle(metricDetail.curveStyle);

        // choose the axis
        QwtAxisId axisid = chooseYAxis(metricDetail.uunits);
        current->setYAxis(axisid);

        // left and right offset for bars
        double left = 0;
        double right = 0;

        if (metricDetail.curveStyle == QwtPlotCurve::Steps) {

            int barn = metricDetail.stack ? stacknum : barnum;

            double space = double(0.9) / bars;
            double gap = space * 0.10;
            double width = space * 0.90;
            left = (space * barn) + (gap / 2) + 0.1;
            right = left + width;

            if (metricDetail.stack && donestack == false) {
                barnum++;
                donestack = true;
            } else if (metricDetail.stack == false) barnum++;
        }

        if (metricDetail.curveStyle == QwtPlotCurve::Steps) {
            
            // fill the bars
            QColor brushColor = metricDetail.penColor;
            if (metricDetail.stack == true) {
                brushColor.setAlpha(255);
                QBrush brush = QBrush(brushColor);
                current->setBrush(brush);
            } else {
                brushColor.setAlpha(200); // now side by side, less transparency required
                QColor brushColor1 = brushColor.darker();

                QLinearGradient linearGradient(0, 0, 0, height());
                linearGradient.setColorAt(0.0, brushColor1);
                linearGradient.setColorAt(1.0, brushColor);
                linearGradient.setSpread(QGradient::PadSpread);
                current->setBrush(linearGradient);
            }

            current->setPen(QPen(Qt::NoPen));
            current->setCurveAttribute(QwtPlotCurve::Inverted, true);

            QwtSymbol *sym = new QwtSymbol;
            sym->setStyle(QwtSymbol::NoSymbol);
            current->setSymbol(sym);

            // fudge for date ranges, not for time of day graph
            // and fudge qwt'S lack of a decent bar chart
            // add a zero point at the head and tail so the
            // histogram columns look nice.
            // and shift all the x-values left by 0.5 so that
            // they centre over x-axis labels
            int i=0;
            for (i=0; i<count; i++) xdata[i] -= 0.5;
            // now add a final 0 value to get the last
            // column drawn - no resize neccessary
            // since it is always sized for 1 + maxnumber of entries
            xdata[i] = xdata[i-1] + 1;
            ydata[i] = 0;
            count++;

            QVector<double> xaxis (xdata.size() * 4);
            QVector<double> yaxis (ydata.size() * 4);

            // samples to time
            for (int i=0, offset=0; i<xdata.size(); i++) {

                double x = (double) xdata[i];
                double y = (double) ydata[i];

                xaxis[offset] = x +left;
                yaxis[offset] = metricDetail.baseline; // use baseline not 0, default is 0
                offset++;
                xaxis[offset] = x+left;
                yaxis[offset] = y;
                offset++;
                xaxis[offset] = x+right;
                yaxis[offset] = y;
                offset++;
                xaxis[offset] = x +right;
                yaxis[offset] = metricDetail.baseline;; // use baseline not 0, default is 0
                offset++;
            }
            xdata = xaxis;
            ydata = yaxis;
            count *= 4;
            // END OF FUDGE

        }

        // set the data series
        current->setSamples(xdata.data(),ydata.data(), count + 1);
        current->setBaseline(metricDetail.baseline);

        // update stack data so we can index off them
        // in tooltip
        *stackX[stackcounter+1] = xdata;
        *stackY[stackcounter+1] = ydata;
        
        // update min/max Y values for the chosen axis
        if (current->maxYValue() > maxY[supportedAxes.indexOf(axisid)]) maxY[supportedAxes.indexOf(axisid)] = current->maxYValue();
        if (current->minYValue() < minY[supportedAxes.indexOf(axisid)]) minY[supportedAxes.indexOf(axisid)] = current->minYValue();

        current->attach(this);

    } // end of reverse for stacked plots

    //qDebug()<<"First plotting iteration.."<<timer.elapsed();

    // do all curves excepts stacks in order
    // we skip stacked entries because they
    // are painted in reverse order in a
    // loop before this one.
    stackcounter= 0;
    for(int m=0; m<settings->metrics.count(); m++) { 

        MetricDetail metricDetail = settings->metrics[m];

        //
        // *ONLY* PLOT NON-STACKS
        //
        if (metricDetail.stack == true) continue;

        QVector<double> xdata, ydata;

        int count;
        if (settings->groupBy != LTM_TOD)
            createCurveData(context, settings, metricDetail, xdata, ydata, count);
        else
            createTODCurveData(context, settings, metricDetail, xdata, ydata, count);

        //qDebug()<<"Create curve data.."<<timer.elapsed();

        // Create a curve
        QwtPlotCurve *current = new QwtPlotCurve(metricDetail.uname);
        current->setVisible(!metricDetail.hidden);
        settings->metrics[m].curve = current;
        if (metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS)
            curves.insert(metricDetail.bestSymbol, current);
        else
            curves.insert(metricDetail.symbol, current);
        if (appsettings->value(this, GC_ANTIALIAS, true).toBool() == true)
            current->setRenderHint(QwtPlotItem::RenderAntialiased);
        QPen cpen = QPen(metricDetail.penColor);
        cpen.setWidth(width);
        current->setPen(cpen);
        current->setStyle(metricDetail.curveStyle);

        // choose the axis
        QwtAxisId axisid = chooseYAxis(metricDetail.uunits);
        current->setYAxis(axisid);

        // left and right offset for bars
        double left = 0;
        double right = 0;
        double middle = 0;
        if (metricDetail.curveStyle == QwtPlotCurve::Steps) {

            // we still worry about stacked bars, since we
            // need to take into account the space it will
            // consume when plotted in the second iteration
            // below this one
            int barn = metricDetail.stack ? stacknum : barnum;

            double space = double(0.9) / bars;
            double gap = space * 0.10;
            double width = space * 0.90;
            left = (space * barn) + (gap / 2) + 0.1;
            right = left + width;
            middle = ((left+right) / double(2)) - 0.5;
            if (metricDetail.stack && donestack == false) {
                barnum++;
                donestack = true;
            } else if (metricDetail.stack == false) barnum++;
        }

        // trend - clone the data for the curve and add a curvefitted
        if (metricDetail.trendtype) {

            // linear regress
            if (metricDetail.trendtype == 1 && count > 2) {

                // override class variable as doing it temporarily for trend line only
                double maxX = 0.5 + groupForDate(settings->end.date(), settings->groupBy) -
                    groupForDate(settings->start.date(), settings->groupBy);

                QString trendName = QString(tr("%1 trend")).arg(metricDetail.uname);
                QString trendSymbol = QString("%1_trend")
                                       .arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ? 
                                       metricDetail.bestSymbol : metricDetail.symbol);

                QwtPlotCurve *trend = new QwtPlotCurve(trendName);
                trend->setVisible(!metricDetail.hidden);

                // cosmetics
                QPen cpen = QPen(metricDetail.penColor.darker(200));
                cpen.setWidth(2); // double thickness for trend lines
                cpen.setStyle(Qt::SolidLine);
                trend->setPen(cpen);
                if (appsettings->value(this, GC_ANTIALIAS, true).toBool()==true)
                    trend->setRenderHint(QwtPlotItem::RenderAntialiased);
                trend->setBaseline(0);
                trend->setYAxis(axisid);
                trend->setStyle(QwtPlotCurve::Lines);

                // perform linear regression
                LTMTrend regress(xdata.data(), ydata.data(), count);
                double xtrend[2], ytrend[2];
                xtrend[0] = 0.0; 
                ytrend[0] = regress.getYforX(0.0);
                // point 2 is at far right of chart, not the last point
                // since we may be forecasting...
                xtrend[1] = maxX;
                ytrend[1] = regress.getYforX(maxX);
                trend->setSamples(xtrend,ytrend, 2);

                trend->attach(this);
                curves.insert(trendSymbol, trend);

            }

            // quadratic lsm regression
            if (metricDetail.trendtype == 2 && count > 3) {
                QString trendName = QString(tr("%1 trend")).arg(metricDetail.uname);
                QString trendSymbol = QString("%1_trend")
                                       .arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ? 
                                       metricDetail.bestSymbol : metricDetail.symbol);

                QwtPlotCurve *trend = new QwtPlotCurve(trendName);
                trend->setVisible(!metricDetail.hidden);

                // cosmetics
                QPen cpen = QPen(metricDetail.penColor.darker(200));
                cpen.setWidth(2); // double thickness for trend lines
                cpen.setStyle(Qt::SolidLine);
                trend->setPen(cpen);
                if (appsettings->value(this, GC_ANTIALIAS, true).toBool()==true)
                    trend->setRenderHint(QwtPlotItem::RenderAntialiased);
                trend->setBaseline(0);
                trend->setYAxis(axisid);
                trend->setStyle(QwtPlotCurve::Lines);

                // perform quadratic curve fit to data
                LTMTrend2 regress(xdata.data(), ydata.data(), count+1);

                QVector<double> xtrend;
                QVector<double> ytrend;

                double inc = (regress.maxx - regress.minx) / 100;
                for (double i=regress.minx; i<=(regress.maxx+inc); i+= inc) {
                    xtrend << i;
                    ytrend << regress.yForX(i);
                }

                // point 2 is at far right of chart, not the last point
                // since we may be forecasting...
                trend->setSamples(xtrend.data(),ytrend.data(), xtrend.count());

                trend->attach(this);
                curves.insert(trendSymbol, trend);
            }
        }

        // highlight outliers
        if (metricDetail.topOut > 0 && metricDetail.topOut < count && count > 10) {

            LTMOutliers outliers(xdata.data(), ydata.data(), count, 10);

            // the top 5 outliers
            QVector<double> hxdata, hydata;
            hxdata.resize(metricDetail.topOut);
            hydata.resize(metricDetail.topOut);

            // QMap orders the list so start at the top and work
            // backwards
            for (int i=0; i<metricDetail.topOut; i++) {
                hxdata[i] = outliers.getXForRank(i) + middle;
                hydata[i] = outliers.getYForRank(i);
            }

            // lets setup a curve with this data then!
            QString outName;
            if (metricDetail.topOut > 1)
                outName = QString(tr("%1 Top %2 Outliers"))
                          .arg(metricDetail.uname)
                          .arg(metricDetail.topOut);
            else
                outName = QString(tr("%1 Outlier")).arg(metricDetail.uname);

            QString outSymbol = QString("%1_outlier").arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ?
                                                          metricDetail.bestSymbol : metricDetail.symbol);
            QwtPlotCurve *out = new QwtPlotCurve(outName);
            out->setVisible(!metricDetail.hidden);
            curves.insert(outSymbol, out);

            out->setRenderHint(QwtPlotItem::RenderAntialiased);
            out->setStyle(QwtPlotCurve::Dots);

            // we might have hidden the symbols for this curve
            // if its set to none then default to a rectangle
            QwtSymbol *sym = new QwtSymbol;
            if (metricDetail.symbolStyle == QwtSymbol::NoSymbol) {
                sym->setStyle(QwtSymbol::Ellipse);
                sym->setSize(10);
            } else {
                sym->setStyle(metricDetail.symbolStyle);
                sym->setSize(20);
            }
            QColor lighter = metricDetail.penColor;
            lighter.setAlpha(50);
            sym->setPen(metricDetail.penColor);
            sym->setBrush(lighter);

            out->setSymbol(sym);
            out->setSamples(hxdata.data(),hydata.data(), metricDetail.topOut);
            out->setBaseline(0);
            out->setYAxis(axisid);
            out->attach(this);
        }

        // highlight lowest / top N values
        if (metricDetail.lowestN > 0 || metricDetail.topN > 0) {

            QMap<double, int> sortedList;

            // copy the yvalues, retaining the offset
            for(int i=0; i<ydata.count(); i++) {
                // pmc metrics we highlight TROUGHS
                if (metricDetail.type == METRIC_STRESS || metricDetail.type == METRIC_PM) {
                    if (i && i < (ydata.count()-1) // not at start/end
                        && ((ydata[i-1] > ydata[i] && ydata[i+1] > ydata[i]) || // is a trough 
                            (ydata[i-1] < ydata[i] && ydata[i+1] < ydata[i])))  // is a peak 
                        sortedList.insert(ydata[i], i);
                } else 
                    sortedList.insert(ydata[i], i);
            }

            // copy the top N values
            QVector<double> hxdata, hydata;
            hxdata.resize(metricDetail.topN + metricDetail.lowestN);
            hydata.resize(metricDetail.topN + metricDetail.lowestN);

            // QMap orders the list so start at the top and work
            // backwards for topN
            QMapIterator<double, int> i(sortedList);
            i.toBack();
            int counter = 0;
            while (i.hasPrevious() && counter < metricDetail.topN) {
                i.previous();
                if (ydata[i.value()]) {
                    hxdata[counter] = xdata[i.value()] + middle;
                    hydata[counter] = ydata[i.value()];
                    counter++;
                }
            }
            i.toFront();
            counter = 0; // and forwards for bottomN
            while (i.hasNext() && counter < metricDetail.lowestN) {
                i.next();
                if (ydata[i.value()]) {
                    hxdata[metricDetail.topN + counter] = xdata[i.value()] + middle;
                    hydata[metricDetail.topN + counter] = ydata[i.value()];
                    counter++;
                }
            }

            // lets setup a curve with this data then!
            QString topName;
            if (counter > 1)
                topName = QString(tr("%1 Best"))
                          .arg(metricDetail.uname);
            else
                topName = QString(tr("Best %1")).arg(metricDetail.uname);

            QString topSymbol = QString("%1_topN")
                                .arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ? 
                                     metricDetail.bestSymbol : metricDetail.symbol);
            QwtPlotCurve *top = new QwtPlotCurve(topName);
            top->setVisible(!metricDetail.hidden);
            curves.insert(topName, top);

            top->setRenderHint(QwtPlotItem::RenderAntialiased);
            top->setStyle(QwtPlotCurve::Dots);

            // we might have hidden the symbols for this curve
            // if its set to none then default to a rectangle
            QwtSymbol *sym = new QwtSymbol;
            if (metricDetail.symbolStyle == QwtSymbol::NoSymbol) {
                sym->setStyle(QwtSymbol::Ellipse);
                sym->setSize(6);
            } else {
                sym->setStyle(metricDetail.symbolStyle);
                sym->setSize(12);
            }
            QColor lighter = metricDetail.penColor;
            lighter.setAlpha(200);
            sym->setPen(metricDetail.penColor);
            sym->setBrush(lighter);

            top->setSymbol(sym);
            top->setSamples(hxdata.data(),hydata.data(), counter);
            top->setBaseline(0);
            top->setYAxis(axisid);
            top->attach(this);

            // if we haven't already got data labels selected for this curve
            // then lets put some on, just for the topN, since they are of
            // interest to the user and typically the first thing they do
            // is move mouse over to get a tooltip anyway!
            if (!metricDetail.labels) {

                QFont labelFont;
                labelFont.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
                labelFont.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

                // loop through each NONZERO value and add a label
                for  (int i=0; i<hxdata.count(); i++) {

                    double value = hydata[i];

                    // bar headings always need to be centered
                    if (value) {

                        // format the label appropriately
                        const RideMetric *m = metricDetail.metric;
                        QString labelString;

                        if (m != NULL) {

                            // handle precision of 1 for seconds converted to hours
                            int precision = m->precision();
                            if (metricDetail.uunits == "seconds" || metricDetail.uunits == tr("seconds")) precision=1;
                            if (metricDetail.uunits == "km") precision=0;

                            // we have a metric so lets be precise ...
                            labelString = QString("%1").arg(value, 0, 'f', precision);

                        } else {
                            // no precision
                            labelString = (QString("%1").arg(value, 0, 'f', 1));
                        }


                        // Qwt uses its own text objects
                        QwtText text(labelString);
                        text.setFont(labelFont);
                        text.setColor(metricDetail.penColor);

                        // make that mark -- always above with topN
                        QwtPlotMarker *label = new QwtPlotMarker();
                        label->setVisible(!metricDetail.hidden);
                        label->setLabel(text);
                        label->setValue(hxdata[i], hydata[i]);
                        label->setYAxis(axisid);
                        label->setSpacing(6); // not px but by yaxis value !? mad.
                        label->setLabelAlignment(Qt::AlignTop | Qt::AlignCenter);

                        // and attach
                        label->attach(this);
                        labels << label;
                    }
                }
            }
        }

        if (metricDetail.curveStyle == QwtPlotCurve::Steps) {
            
            // fill the bars
            QColor brushColor = metricDetail.penColor;
            brushColor.setAlpha(200); // now side by side, less transparency required
            QColor brushColor1 = metricDetail.penColor.darker();
            QLinearGradient linearGradient(0, 0, 0, height());
            linearGradient.setColorAt(0.0, brushColor1);
            linearGradient.setColorAt(1.0, brushColor);
            linearGradient.setSpread(QGradient::PadSpread);
            current->setBrush(linearGradient);
            current->setPen(QPen(Qt::NoPen));
            current->setCurveAttribute(QwtPlotCurve::Inverted, true);

            QwtSymbol *sym = new QwtSymbol;
            sym->setStyle(QwtSymbol::NoSymbol);
            current->setSymbol(sym);

            // fudge for date ranges, not for time of day graph
            // fudge qwt'S lack of a decent bar chart
            // add a zero point at the head and tail so the
            // histogram columns look nice.
            // and shift all the x-values left by 0.5 so that
            // they centre over x-axis labels
            count = xdata.size()-2;

            int i=0;
            for (i=0; i<count; i++) xdata[i] -= 0.5;
            // now add a final 0 value to get the last
            // column drawn - no resize neccessary
            // since it is always sized for 1 + maxnumber of entries
            xdata[i] = xdata[i-1] + 1;
            ydata[i] = 0;
            count++;

            QVector<double> xaxis (xdata.size() * 4);
            QVector<double> yaxis (ydata.size() * 4);

            // samples to time
            for (int i=0, offset=0; i<xdata.size(); i++) {

                double x = (double) xdata[i];
                double y = (double) ydata[i];

                xaxis[offset] = x +left;
                yaxis[offset] = metricDetail.baseline;; // use baseline not 0, default is 0
                offset++;
                xaxis[offset] = x+left;
                yaxis[offset] = y;
                offset++;
                xaxis[offset] = x+right;
                yaxis[offset] = y;
                offset++;
                xaxis[offset] = x +right;
                yaxis[offset] = metricDetail.baseline;; // use baseline not 0, default is 0
                offset++;
            }
            xdata = xaxis;
            ydata = yaxis;
            count *= 4;
            // END OF FUDGE

        } else if (metricDetail.curveStyle == QwtPlotCurve::Lines) {

            QPen cpen = QPen(metricDetail.penColor);
            cpen.setWidth(width);
            QwtSymbol *sym = new QwtSymbol;
            sym->setSize(6);
            sym->setStyle(metricDetail.symbolStyle);
            sym->setPen(QPen(metricDetail.penColor));
            sym->setBrush(QBrush(metricDetail.penColor));
            current->setSymbol(sym);
            current->setPen(cpen);

            // fill below the line
            if (metricDetail.fillCurve) {
                QColor fillColor = metricDetail.penColor;
                fillColor.setAlpha(100);
                current->setBrush(fillColor);
            }


        } else if (metricDetail.curveStyle == QwtPlotCurve::Dots) {

            QwtSymbol *sym = new QwtSymbol;
            sym->setSize(6);
            sym->setStyle(metricDetail.symbolStyle);
            sym->setPen(QPen(metricDetail.penColor));
            sym->setBrush(QBrush(metricDetail.penColor));
            current->setSymbol(sym);

        } else if (metricDetail.curveStyle == QwtPlotCurve::Sticks) {

            QwtSymbol *sym = new QwtSymbol;
            sym->setSize(4);
            sym->setStyle(metricDetail.symbolStyle);
            sym->setPen(QPen(metricDetail.penColor));
            sym->setBrush(QBrush(Qt::white));
            current->setSymbol(sym);

        }

        // add data labels
        if (metricDetail.labels) {

            QFont labelFont;
            labelFont.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
            labelFont.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

            // loop through each NONZERO value and add a label
            for  (int i=0; i<xdata.count(); i++) {

                // we only want to do once per bar, which has 4 points
                if (metricDetail.curveStyle == QwtPlotCurve::Steps && (i+1)%4) continue;

                double value = metricDetail.curveStyle == QwtPlotCurve::Steps ? ydata[i-1] : ydata[i];

                // bar headings always need to be centered
                if (value) {

                    // format the label appropriately
                    const RideMetric *m = metricDetail.metric;
                    QString labelString;

                    if (m != NULL) {

                        // handle precision of 1 for seconds converted to hours
                        int precision = m->precision();
                        if (metricDetail.uunits == "seconds"  || metricDetail.uunits == tr("seconds")) precision=1;
                        if (metricDetail.uunits == "km") precision=0;

                        // we have a metric so lets be precise ...
                        labelString = QString("%1").arg(value, 0, 'f', precision);

                    } else {
                        // no precision
                        labelString = (QString("%1").arg(value, 0, 'f', 1));
                    }


                    // Qwt uses its own text objects
                    QwtText text(labelString);
                    text.setFont(labelFont);
                    text.setColor(metricDetail.penColor);

                    // make that mark
                    QwtPlotMarker *label = new QwtPlotMarker();
                    label->setVisible(!metricDetail.hidden);
                    label->setLabel(text);
                    label->setValue(xdata[i], ydata[i]);
                    label->setYAxis(axisid);
                    label->setSpacing(3); // not px but by yaxis value !? mad.

                    // Bars(steps) / sticks / dots: label above centered
                    // but bars have multiple points offset from their actual
                    // so need to adjust bars to centre above the top of the bar
                    if (metricDetail.curveStyle == QwtPlotCurve::Steps) {

                        // We only get every fourth point, so center
                        // between second and third point of bar "square"
                        label->setValue((xdata[i-1]+xdata[i-2])/2.00f, ydata[i-1]);
                    }

                    // Lables on a Line curve should be above/below depending upon the shape of the curve
                    if (metricDetail.curveStyle == QwtPlotCurve::Lines) {

                        label->setLabelAlignment(Qt::AlignTop | Qt::AlignCenter);

                        // we could simplify this into one if clause but it wouldn't be
                        // so obvious what we were doing
                        if (i && (i == ydata.count()-3) && ydata[i-1] > ydata[i]) {

                            // last point on curve
                            label->setLabelAlignment(Qt::AlignBottom | Qt::AlignCenter);

                        } else if (i && i < ydata.count()) {

                            // is a low / valley
                            if (ydata[i-1] > ydata[i] && ydata[i+1] > ydata[i])
                                label->setLabelAlignment(Qt::AlignBottom | Qt::AlignCenter);

                        } else if (i == 0 && ydata[i+1] > ydata[i]) {

                            // first point on curve
                            label->setLabelAlignment(Qt::AlignBottom | Qt::AlignCenter);
                        }

                    } else {

                        label->setLabelAlignment(Qt::AlignTop | Qt::AlignCenter);
                    }

                    // and attach
                    label->attach(this);
                    labels << label;
                }
            }
        }

        // smoothing
        if (metricDetail.smooth == true) {
            current->setCurveAttribute(QwtPlotCurve::Fitted, true);
        }

        // set the data series
        current->setSamples(xdata.data(),ydata.data(), count + 1);
        current->setBaseline(metricDetail.baseline);

        //qDebug()<<"Set Curve Data.."<<timer.elapsed();

        // update min/max Y values for the chosen axis
        if (current->maxYValue() > maxY[supportedAxes.indexOf(axisid)]) maxY[supportedAxes.indexOf(axisid)] = current->maxYValue();
        if (current->minYValue() < minY[supportedAxes.indexOf(axisid)]) minY[supportedAxes.indexOf(axisid)] = current->minYValue();

        current->attach(this);

    }

    //qDebug()<<"Second plotting iteration.."<<timer.elapsed();


    if (settings->groupBy != LTM_TOD) {

        // make start date always fall on a Monday
        if (settings->groupBy == LTM_WEEK) {
            int dow = settings->start.date().dayOfWeek(); // 1-7, where 1=monday
            settings->start.date().addDays(dow-1*-1);
        }

        // setup the xaxis at the bottom
        int tics;
        maxX = 0.5 + groupForDate(settings->end.date(), settings->groupBy) -
                groupForDate(settings->start.date(), settings->groupBy);
        if (maxX < 14) {
            tics = 1;
        } else {
            tics = 1 + maxX/10;
        }
        setAxisScale(xBottom, -0.5, maxX, tics);
        setAxisScaleDraw(QwtPlot::xBottom, new LTMScaleDraw(settings->start,
                    groupForDate(settings->start.date(), settings->groupBy), settings->groupBy));

    } else {
        setAxisScale(xBottom, 0, 24, 2);
        setAxisScaleDraw(QwtPlot::xBottom, new LTMScaleDraw(settings->start,
                    groupForDate(settings->start.date(), settings->groupBy), settings->groupBy));
    }
    enableAxis(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xTop, false);

    // run through the Y axis
    for (int i=0; i<8; i++) {
        // set the scale on the axis
        if (i != xBottom && i != xTop) {
            maxY[i] *= 1.1; // add 10% headroom
            if (maxY[i] == minY[i] && maxY[i] == 0)
                setAxisScale(supportedAxes[i], 0.0f, 100.0f); // to stop ugly
            else
                setAxisScale(supportedAxes[i], minY[i], maxY[i]);
        }
    }

    QString format = axisTitle(yLeft).text();
    picker->setAxes(xBottom, yLeft);
    picker->setFormat(format);

    // draw zone labels axisid of -1 means delete whats there
    // cause no watts are being displayed
    if (settings->shadeZones == true) {
        QwtAxisId axisid = axes.value("watts", QwtAxisId(-1,-1));
        if (axisid == QwtAxisId(-1,-1)) axisid = axes.value(tr("watts"), QwtAxisId(-1,-1)); // Try translated version
        refreshZoneLabels(axisid);
    } else {
        refreshZoneLabels(QwtAxisId(-1,-1)); // turn em off
    }

    QHashIterator<QString, QwtPlotCurve*> p(curves);
    while (p.hasNext()) {
        p.next();

        // always hide bollocksy curves
        if (p.key().endsWith(tr("trend")) || p.key().endsWith(tr("Outliers")) || p.key().endsWith(tr("Best")) || p.key().startsWith(tr("Best")))
            p.value()->setItemAttribute(QwtPlotItem::Legend, false);
        else
            p.value()->setItemAttribute(QwtPlotItem::Legend, settings->legend);
    }

    // show legend?
    if (settings->legend == false) this->legend()->hide();
    else this->legend()->show();


    // now refresh
    updateLegend();

    // markers
    if (settings->groupBy != LTM_TOD)
        refreshMarkers(settings, settings->start.date(), settings->end.date(), settings->groupBy, GColor(CPLOTMARKER));

    //qDebug()<<"Final tidy.."<<timer.elapsed();

    // update colours etc for plot chrome will also save state
    configChanged(CONFIG_APPEARANCE);

    // plot
    replot();

    //qDebug()<<"Replot and done.."<<timer.elapsed();

}

void
LTMPlot::setCompareData(LTMSettings *set)
{
    QTime timer;
    timer.start();

    MAXX=0.0; // maximum value for x, always from 0-n
    settings = set;

    //qDebug()<<"Starting.."<<timer.elapsed();

    // wipe existing curves/axes details
    QHashIterator<QString, QwtPlotCurve*> c(curves);
    while (c.hasNext()) {
        c.next();
        QString symbol = c.key();
        QwtPlotCurve *current = c.value();
        //current->detach(); // the destructor does this for you
        delete current;
    }
    curves.clear();
    if (highlighter) {
        highlighter->detach();
        delete highlighter;
        highlighter = NULL;
    }
    foreach (QwtPlotMarker *label, labels) {
        label->detach();
        delete label;
    }
    labels.clear();
    // clear old markers - if there are any
    foreach(QwtPlotMarker *m, markers) {
        m->detach();
        delete m;
    }
    markers.clear();


    // disable all y axes until we have populated
    for (int i=0; i<8; i++) {
        setAxisVisible(supportedAxes[i], false);
        enableAxis(supportedAxes[i].id, false);
    }
    axes.clear();
    axesObject.clear();
    axesId.clear();

    // reset all min/max Y values
    for (int i=0; i<10; i++) minY[i]=0, maxY[i]=0;

    // which yAxis did we use (should be yLeft)
    QwtAxisId axisid(QwtPlot::yLeft, 0);

    // which compare date range are we on?
    int cdCount =0;

    // how many bars?
    int bars =0;
    foreach(CompareDateRange cd, context->compareDateRanges) if (cd.checked) bars++;

    //
    // Setup curve for every Date Range being plotted
    //
    foreach(CompareDateRange cd, context->compareDateRanges) {

        // only plot date ranges selected!
        if (!cd.checked) continue;

        // increment count of date ranges we have
        cdCount++;

        //QColor color;
        //QDate start, end;
        //int days;
        //Context *sourceContext;

        // no data to display so that all folks
        if (context->athlete->rideCache->rides().count() == 0) continue;

        QDateTime first = context->athlete->rideCache->rides().first()->dateTime;
        QDateTime last = context->athlete->rideCache->rides().last()->dateTime;

        // end
        if (settings->end == QDateTime() ||
            settings->end > last.addDays(365)) {
            if (settings->end < QDateTime::currentDateTime()) {
                settings->end = QDateTime::currentDateTime();
            } else {
                settings->end = last;
            }
        }

        // start
        if (settings->start == QDateTime() ||
            settings->start < first.addDays(-365)) {
            settings->start = first;
        }


        settings->start = QDateTime(cd.start, QTime());
        settings->end = QDateTime(cd.end, QTime());

        // For each metric in chart, translate units and name if default uname
        //XXX BROKEN XXX LTMTool::translateMetrics(context, settings);

        // we need to do this for each date range as they are dependant
        // on the metrics chosen and can't be pre-cached
        settings->specification.setDateRange(DateRange(cd.start, cd.end));

        // bests...
        QList<RideBest> herebests;
        herebests = RideFileCache::getAllBestsFor(cd.sourceContext, settings->metrics, settings->specification);
        settings->bests = &herebests;

        switch (settings->groupBy) {
            case LTM_TOD:
                setAxisTitle(xBottom, tr("Time of Day"));
                break;
            case LTM_DAY:
                setAxisTitle(xBottom, tr("Day"));
                break;
            case LTM_WEEK:
                setAxisTitle(xBottom, tr("Week"));
                break;
            case LTM_MONTH:
                setAxisTitle(xBottom, tr("Month"));
                break;
            case LTM_YEAR:
                setAxisTitle(xBottom, tr("Year"));
                break;
            case LTM_ALL:
                setAxisTitle(xBottom, tr("All"));
                break;
            default:
                setAxisTitle(xBottom, tr("Date"));
                break;
        }
        enableAxis(QwtAxis::xBottom, true);
        setAxisVisible(QwtAxis::xBottom, true);
        setAxisVisible(QwtAxis::xTop, false);


        //qDebug()<<"Wiped previous.."<<timer.elapsed();

        // count the bars since we format them side by side and need
        // to now how to offset them from each other
        // unset stacking if not a bar chart too since we don't support
        // that yet, but would be good to add in the future (stacked
        // area plot).

        // index through rather than foreach so we can modify

        // aggregate the stack curves - backwards since
        // we plot forwards overlaying to create the illusion
        // of a stack, when in fact its just bars of descending
        // order (with values aggregated)

        // free stack memory
        foreach(QVector<double>*p, stackX) delete p;
        foreach(QVector<double>*q, stackY) delete q;
        stackX.clear();
        stackY.clear();
        stacks.clear();

        int r=0;
        foreach (MetricDetail metricDetail, settings->metrics) {
            if (metricDetail.stack == true) {

                // register this data
                QVector<double> *xdata = new QVector<double>();
                QVector<double> *ydata = new QVector<double>();
                stackX.append(xdata);
                stackY.append(ydata);

                int count;
                if (settings->groupBy != LTM_TOD)
                    createCurveData(cd.sourceContext, settings, metricDetail, *xdata, *ydata, count);
                else
                    createTODCurveData(cd.sourceContext, settings, metricDetail, *xdata, *ydata, count);

                // lets catch the x-scale
                if (count > MAXX) MAXX=count;

                // we add in the last curve for X axis values
                if (r) {
                    aggregateCurves(*stackY[r], *stackY[r-1]);
                }
                r++;
            }
        }

        //qDebug()<<"Created curve data.."<<timer.elapsed();

        // setup the curves
        double width = appsettings->value(this, GC_LINEWIDTH, 0.5).toDouble();

        // now we iterate over the metric details AGAIN
        // but this time in reverse and only plot the
        // stacked values. This is because we overcome the
        // lack of a stacked plot in QWT by painting decreasing
        // bars, with the values aggregated previously
        // so if we plot L1 time in zone 1hr and L2 time in zone 1hr
        // it plots as L2 time in zone 2hr and then paints over that
        // with a L1 time in zone of 1hr.
        //
        // The tooltip has to unpick the aggregation to ensure
        // that it subtracts other data series in the stack from
        // the value plotted... all nasty but heck, it works
        int stackcounter = stackX.size()-1;
        for (int m=settings->metrics.count()-1; m>=0; m--) {

            //
            // *ONLY* PLOT STACKS
            //

            int count=0;
            MetricDetail metricDetail = settings->metrics[m];

            if (metricDetail.stack == false) continue;

            QVector<double> xdata, ydata;

            // use the aggregated values
            xdata = *stackX[stackcounter];
            ydata = *stackY[stackcounter];
            stackcounter--;
            count = xdata.size()-2;

            // no data to plot!
            if (count <= 0) continue;

            // name is year and metric
            QString name = QString ("%1 %2").arg(cd.name).arg(metricDetail.uname);

            // Create a curve
            QwtPlotCurve *current = new QwtPlotCurve(name);
            if (metricDetail.type == METRIC_BEST)
                curves.insert(name, current);
            else
                curves.insert(name, current);
            stacks.insert(current, stackcounter+1);
            if (appsettings->value(this, GC_ANTIALIAS, true).toBool() == true)
                current->setRenderHint(QwtPlotItem::RenderAntialiased);
            QPen cpen = QPen(cd.color);
            cpen.setWidth(width);
            current->setPen(cpen);
            current->setStyle(metricDetail.curveStyle);

            // choose the axis
            axisid = chooseYAxis(metricDetail.uunits);
            current->setYAxis(axisid);

            // left and right offset for bars
            double left = 0;
            double right = 0;

            if (metricDetail.curveStyle == QwtPlotCurve::Steps) {

                int barn = cdCount-1;

                double space = double(0.9) / bars;
                double gap = space * 0.10;
                double width = space * 0.90;
                left = (space * barn) + (gap / 2) + 0.1;
                right = left + width;

                //left -= 1.00f;
                //right -= 1.00f;
                //left -= 0.5 + gap;
                //right -= 0.5 + gap;
            }

            if (metricDetail.curveStyle == QwtPlotCurve::Steps) {
            
                // fill the bars
                QColor merge;
                merge.setRed((metricDetail.penColor.red() + cd.color.red()) / 2);
                merge.setGreen((metricDetail.penColor.green() + cd.color.green()) / 2);
                merge.setBlue((metricDetail.penColor.blue() + cd.color.blue()) / 2);

                QColor brushColor = merge;
                if (metricDetail.stack == true) {
                    brushColor.setAlpha(255);
                    QBrush brush = QBrush(brushColor);
                    current->setBrush(brush);
                } else {
                    brushColor.setAlpha(200); // now side by side, less transparency required
                    QColor brushColor1 = brushColor.darker();
    
                    QLinearGradient linearGradient(0, 0, 0, height());
                    linearGradient.setColorAt(0.0, brushColor1);
                    linearGradient.setColorAt(1.0, brushColor);
                    linearGradient.setSpread(QGradient::PadSpread);
                    current->setBrush(linearGradient);
                }

                current->setPen(QPen(Qt::NoPen));
                current->setCurveAttribute(QwtPlotCurve::Inverted, true);

                QwtSymbol *sym = new QwtSymbol;
                sym->setStyle(QwtSymbol::NoSymbol);
                current->setSymbol(sym);

                // fudge for date ranges, not for time of day graph
                // and fudge qwt'S lack of a decent bar chart
                // add a zero point at the head and tail so the
                // histogram columns look nice.
                // and shift all the x-values left by 0.5 so that
                // they centre over x-axis labels
                int i=0;
                for (i=0; i<count; i++) xdata[i] -= 0.5;
                // now add a final 0 value to get the last
                // column drawn - no resize neccessary
                // since it is always sized for 1 + maxnumber of entries
                xdata[i] = xdata[i-1] + 1;
                ydata[i] = 0;
                count++;

                QVector<double> xaxis (xdata.size() * 4);
                QVector<double> yaxis (ydata.size() * 4);

                // samples to time
                for (int i=0, offset=0; i<xdata.size(); i++) {

                    double x = (double) xdata[i];
                    double y = (double) ydata[i];

                    xaxis[offset] = x +left;
                    yaxis[offset] = metricDetail.baseline; // use baseline not 0, default is 0
                    offset++;
                    xaxis[offset] = x+left;
                    yaxis[offset] = y;
                    offset++;
                    xaxis[offset] = x+right;
                    yaxis[offset] = y;
                    offset++;
                    xaxis[offset] = x +right;
                    yaxis[offset] = metricDetail.baseline;; // use baseline not 0, default is 0
                    offset++;
                }
                xdata = xaxis;
                ydata = yaxis;
                count *= 4;
                // END OF FUDGE

            }

            // set the data series
            current->setSamples(xdata.data(),ydata.data(), count + 1);
            current->setBaseline(metricDetail.baseline);

            // update stack data so we can index off them
            // in tooltip
            *stackX[stackcounter+1] = xdata;
            *stackY[stackcounter+1] = ydata;
         
            // update min/max Y values for the chosen axis
            if (current->maxYValue() > maxY[supportedAxes.indexOf(axisid)]) maxY[supportedAxes.indexOf(axisid)] = current->maxYValue();
            if (current->minYValue() < minY[supportedAxes.indexOf(axisid)]) minY[supportedAxes.indexOf(axisid)] = current->minYValue();

            current->attach(this);

        } // end of reverse for stacked plots

        //qDebug()<<"First plotting iteration.."<<timer.elapsed();

        // do all curves excepts stacks in order
        // we skip stacked entries because they
        // are painted in reverse order in a
        // loop before this one.
        stackcounter= 0;
        foreach (MetricDetail metricDetail, settings->metrics) {

            //
            // *ONLY* PLOT NON-STACKS
            //
            if (metricDetail.stack == true) continue;

            QVector<double> xdata, ydata;

            int count=0;
            if (settings->groupBy != LTM_TOD)
                createCurveData(cd.sourceContext, settings, metricDetail, xdata, ydata, count);
            else
                createTODCurveData(cd.sourceContext, settings, metricDetail, xdata, ydata, count);

            // lets catch the x-scale
            if (count > MAXX) MAXX=count;

            //qDebug()<<"Create curve data.."<<timer.elapsed();

            // Create a curve
            QwtPlotCurve *current = new QwtPlotCurve(cd.name);
            if (metricDetail.type == METRIC_BEST)
                curves.insert(cd.name, current);
            else
                curves.insert(cd.name, current);
            if (appsettings->value(this, GC_ANTIALIAS, true).toBool() == true)
                current->setRenderHint(QwtPlotItem::RenderAntialiased);
            QPen cpen = QPen(cd.color);
            cpen.setWidth(width);
            current->setPen(cpen);
            current->setStyle(metricDetail.curveStyle);

            // choose the axis
            axisid = chooseYAxis(metricDetail.uunits);
            current->setYAxis(axisid);

            // left and right offset for bars
            double left = 0;
            double right = 0;
            double middle = 0;
            if (metricDetail.curveStyle == QwtPlotCurve::Steps) {

                // we still worry about stacked bars, since we
                // need to take into account the space it will
                // consume when plotted in the second iteration
                // below this one
                int barn = cdCount-1;

                double space = double(0.9) / bars;
                double gap = space * 0.10;
                double width = space * 0.90;
                left = (space * barn) + (gap / 2) + 0.1;
                right = left + width;
                middle = ((left+right) / double(2)) - 0.5;

            }

            // trend - clone the data for the curve and add a curvefitted
            if (metricDetail.trendtype) {

                // linear regress
                if (metricDetail.trendtype == 1 && count > 2) {

                    // override class variable as doing it temporarily for trend line only
                    double maxX = 0.5 + groupForDate(settings->end.date(), settings->groupBy) -
                        groupForDate(settings->start.date(), settings->groupBy);

                    QString trendName = QString(tr("%1 %2 trend")).arg(cd.name).arg(metricDetail.uname);
                    QString trendSymbol = QString("%1_trend")
                                        .arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ? 
                                        metricDetail.bestSymbol : metricDetail.symbol);

                    QwtPlotCurve *trend = new QwtPlotCurve(trendName);
                    curves.insert(trendName, trend);

                    // cosmetics
                    QPen cpen = QPen(cd.color.darker(200));
                    cpen.setWidth(2); // double thickness for trend lines
                    cpen.setStyle(Qt::SolidLine);
                    trend->setPen(cpen);
                    if (appsettings->value(this, GC_ANTIALIAS, true).toBool()==true)
                        trend->setRenderHint(QwtPlotItem::RenderAntialiased);
                    trend->setBaseline(0);
                    trend->setYAxis(axisid);
                    trend->setStyle(QwtPlotCurve::Lines);

                    // perform linear regression
                    LTMTrend regress(xdata.data(), ydata.data(), count);
                    double xtrend[2], ytrend[2];
                    xtrend[0] = 0.0; 
                    ytrend[0] = regress.getYforX(0.0);
                    // point 2 is at far right of chart, not the last point
                    // since we may be forecasting...
                    xtrend[1] = maxX;
                    ytrend[1] = regress.getYforX(maxX);
                    trend->setSamples(xtrend,ytrend, 2);

                    trend->attach(this);
                }

                // quadratic lsm regression
                if (metricDetail.trendtype == 2 && count > 3) {
                    QString trendName = QString(tr("%1 %2 trend")).arg(cd.name).arg(metricDetail.uname);
                    QString trendSymbol = QString("%1_trend")
                                        .arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ? 
                                        metricDetail.bestSymbol : metricDetail.symbol);

                    QwtPlotCurve *trend = new QwtPlotCurve(trendName);
                    curves.insert(trendName, trend);

                    // cosmetics
                    QPen cpen = QPen(cd.color.darker(200));
                    cpen.setWidth(2); // double thickness for trend lines
                    cpen.setStyle(Qt::SolidLine);
                    trend->setPen(cpen);
                    if (appsettings->value(this, GC_ANTIALIAS, true).toBool()==true)
                    trend->setRenderHint(QwtPlotItem::RenderAntialiased);
                    trend->setBaseline(0);
                    trend->setYAxis(axisid);
                    trend->setStyle(QwtPlotCurve::Lines);
    
                    // perform quadratic curve fit to data
                    LTMTrend2 regress(xdata.data(), ydata.data(), count+1);
    
                    QVector<double> xtrend;
                    QVector<double> ytrend;

                    double inc = (regress.maxx - regress.minx) / 100;
                    for (double i=regress.minx; i<=(regress.maxx+inc); i+= inc) {
                        xtrend << i;
                        ytrend << regress.yForX(i);
                    }

                    // point 2 is at far right of chart, not the last point
                    // since we may be forecasting...
                    trend->setSamples(xtrend.data(),ytrend.data(), xtrend.count());

                    trend->attach(this);
                }
            }

            // highlight outliers
            if (metricDetail.topOut > 0 && metricDetail.topOut < count && count > 10) {

                LTMOutliers outliers(xdata.data(), ydata.data(), count, 10);

                // the top 5 outliers
                QVector<double> hxdata, hydata;
                hxdata.resize(metricDetail.topOut);
                hydata.resize(metricDetail.topOut);

                // QMap orders the list so start at the top and work
                // backwards
                for (int i=0; i<metricDetail.topOut; i++) {
                    hxdata[i] = outliers.getXForRank(i) + middle;
                    hydata[i] = outliers.getYForRank(i);
                }

                // lets setup a curve with this data then!
                QString outName;
                outName = QString(tr("%1 %2 Outliers"))
                          .arg(cd.name)
                          .arg(metricDetail.uname);

                QString outSymbol = QString("%1_outlier").arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ?
                                                            metricDetail.bestSymbol : metricDetail.symbol);
                QwtPlotCurve *out = new QwtPlotCurve(outName);
                curves.insert(outName, out);

                out->setRenderHint(QwtPlotItem::RenderAntialiased);
                out->setStyle(QwtPlotCurve::Dots);

                // we might have hidden the symbols for this curve
                // if its set to none then default to a rectangle
                QwtSymbol *sym = new QwtSymbol;
                if (metricDetail.symbolStyle == QwtSymbol::NoSymbol) {
                    sym->setStyle(QwtSymbol::Ellipse);
                    sym->setSize(10);
                } else {
                    sym->setStyle(metricDetail.symbolStyle);
                    sym->setSize(20);
                }
                QColor lighter = cd.color;
                lighter.setAlpha(50);
                sym->setPen(cd.color);
                sym->setBrush(lighter);

                out->setSymbol(sym);
                out->setSamples(hxdata.data(),hydata.data(), metricDetail.topOut);
                out->setBaseline(0);
                out->setYAxis(axisid);
                out->attach(this);
            }

            // highlight top N values
            if (metricDetail.lowestN > 0 || metricDetail.topN > 0) {

                QMap<double, int> sortedList;

                for(int i=0; i<ydata.count(); i++) {
                    // pmc metrics we highlight TROUGHS
                    if (metricDetail.type == METRIC_STRESS || metricDetail.type == METRIC_PM) {
                        if (i && i < (ydata.count()-1) // not at start/end
                            && ((ydata[i-1] > ydata[i] && ydata[i+1] > ydata[i]) || // is a trough 
                                (ydata[i-1] < ydata[i] && ydata[i+1] < ydata[i])))  // is a peak 
                            sortedList.insert(ydata[i], i);
                    } else 
                        sortedList.insert(ydata[i], i);
                }


                // copy the top N values
                QVector<double> hxdata, hydata;
                hxdata.resize(metricDetail.topN + metricDetail.lowestN);
                hydata.resize(metricDetail.topN + metricDetail.lowestN);

                // QMap orders the list so start at the top and work
                // backwards for topN
                QMapIterator<double, int> i(sortedList);
                i.toBack();
                int counter = 0;
                while (i.hasPrevious() && counter < metricDetail.topN) {
                    i.previous();
                    if (ydata[i.value()]) {
                        hxdata[counter] = xdata[i.value()] + middle;
                        hydata[counter] = ydata[i.value()];
                        counter++;
                    }
                }
                i.toFront();
                counter = 0; // and backwards for bottomN
                while (i.hasNext() && counter < metricDetail.lowestN) {
                    i.next();
                    if (ydata[i.value()]) {
                        hxdata[metricDetail.topN + counter] = xdata[i.value()] + middle;
                        hydata[metricDetail.topN + counter] = ydata[i.value()];
                        counter++;
                    }
                }

                // lets setup a curve with this data then!
                QString topName = QString(tr("%1 %2 Best")).arg(cd.name).arg(metricDetail.uname);
                QString topSymbol = QString("%1_topN")
                                    .arg((metricDetail.type == METRIC_BEST || metricDetail.type == METRIC_STRESS) ? 
                                        metricDetail.bestSymbol : metricDetail.symbol);
                QwtPlotCurve *top = new QwtPlotCurve(topName);
                curves.insert(topName, top);

                top->setRenderHint(QwtPlotItem::RenderAntialiased);
                top->setStyle(QwtPlotCurve::Dots);

                // we might have hidden the symbols for this curve
                // if its set to none then default to a rectangle
                QwtSymbol *sym = new QwtSymbol;
                if (metricDetail.symbolStyle == QwtSymbol::NoSymbol) {
                    sym->setStyle(QwtSymbol::Ellipse);
                    sym->setSize(6);
                } else {
                    sym->setStyle(metricDetail.symbolStyle);
                    sym->setSize(12);
                }
                QColor lighter = cd.color;
                lighter.setAlpha(200);
                sym->setPen(cd.color);
                sym->setBrush(lighter);

                top->setSymbol(sym);
                top->setSamples(hxdata.data(),hydata.data(), counter);
                top->setBaseline(0);
                top->setYAxis(axisid);
                top->attach(this);

                // if we haven't already got data labels selected for this curve
                // then lets put some on, just for the topN, since they are of
                // interest to the user and typically the first thing they do
                // is move mouse over to get a tooltip anyway!
                if (!metricDetail.labels) {

                    QFont labelFont;
                    labelFont.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
                    labelFont.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

                    // loop through each NONZERO value and add a label
                    for  (int i=0; i<hxdata.count(); i++) {

                        double value = hydata[i];

                        // bar headings always need to be centered
                        if (value) {

                            // format the label appropriately
                            const RideMetric *m = metricDetail.metric;
                            QString labelString;

                            if (m != NULL) {

                                // handle precision of 1 for seconds converted to hours
                                int precision = m->precision();
                                if (metricDetail.uunits == "seconds"  || metricDetail.uunits == tr("seconds")) precision=1;
                                if (metricDetail.uunits == "km") precision=0;

                                // we have a metric so lets be precise ...
                                labelString = QString("%1").arg(value , 0, 'f', precision);
    
                            } else {
                                // no precision
                                labelString = (QString("%1").arg(value, 0, 'f', 1));
                            }


                            // Qwt uses its own text objects
                            QwtText text(labelString);
                            text.setFont(labelFont);
                            text.setColor(cd.color);

                            // make that mark -- always above with topN
                            QwtPlotMarker *label = new QwtPlotMarker();
                            label->setLabel(text);
                            label->setValue(hxdata[i], hydata[i]);
                            label->setYAxis(axisid);
                            label->setSpacing(6); // not px but by yaxis value !? mad.
                            label->setLabelAlignment(Qt::AlignTop | Qt::AlignCenter);

                            // and attach
                            label->attach(this);
                            labels << label;
                        }
                    }
                }
            }

            if (metricDetail.curveStyle == QwtPlotCurve::Steps) {
            
                // fill the bars
                QColor brushColor = cd.color;
                brushColor.setAlpha(200); // now side by side, less transparency required
                QColor brushColor1 = cd.color.darker();
                QLinearGradient linearGradient(0, 0, 0, height());
                linearGradient.setColorAt(0.0, brushColor1);
                linearGradient.setColorAt(1.0, brushColor);
                linearGradient.setSpread(QGradient::PadSpread);
                current->setBrush(linearGradient);
                current->setPen(QPen(Qt::NoPen));
                current->setCurveAttribute(QwtPlotCurve::Inverted, true);

                QwtSymbol *sym = new QwtSymbol;
                sym->setStyle(QwtSymbol::NoSymbol);
                current->setSymbol(sym);

                // fudge for date ranges, not for time of day graph
                // fudge qwt'S lack of a decent bar chart
                // add a zero point at the head and tail so the
                // histogram columns look nice.
                // and shift all the x-values left by 0.5 so that
                // they centre over x-axis labels
                count = xdata.size()-2;

                int i=0;
                for (i=0; i<count; i++) xdata[i] -= 0.5;
                // now add a final 0 value to get the last
                // column drawn - no resize neccessary
                // since it is always sized for 1 + maxnumber of entries
                xdata[i] = xdata[i-1] + 1;
                ydata[i] = 0;
                count++;

                QVector<double> xaxis (xdata.size() * 4);
                QVector<double> yaxis (ydata.size() * 4);

                // samples to time
                for (int i=0, offset=0; i<xdata.size(); i++) {

                    double x = (double) xdata[i];
                    double y = (double) ydata[i];

                    xaxis[offset] = x +left;
                    yaxis[offset] = metricDetail.baseline;; // use baseline not 0, default is 0
                    offset++;
                    xaxis[offset] = x+left;
                    yaxis[offset] = y;
                    offset++;
                    xaxis[offset] = x+right;
                    yaxis[offset] = y;
                    offset++;
                    xaxis[offset] = x +right;
                    yaxis[offset] = metricDetail.baseline;; // use baseline not 0, default is 0
                    offset++;
                }
                xdata = xaxis;
                ydata = yaxis;
                count *= 4;
                // END OF FUDGE

            } else if (metricDetail.curveStyle == QwtPlotCurve::Lines) {

                QPen cpen = QPen(cd.color);
                cpen.setWidth(width);
                QwtSymbol *sym = new QwtSymbol;
                sym->setSize(6);
                sym->setStyle(metricDetail.symbolStyle);
                sym->setPen(QPen(cd.color));
                sym->setBrush(QBrush(cd.color));
                current->setSymbol(sym);
                current->setPen(cpen);

                // fill below the line
                if (metricDetail.fillCurve) {
                    QColor fillColor = cd.color;
                    fillColor.setAlpha(100);
                    current->setBrush(fillColor);
                }


            } else if (metricDetail.curveStyle == QwtPlotCurve::Dots) {

                QwtSymbol *sym = new QwtSymbol;
                sym->setSize(6);
                sym->setStyle(metricDetail.symbolStyle);
                sym->setPen(QPen(cd.color));
                sym->setBrush(QBrush(cd.color));
                current->setSymbol(sym);

            } else if (metricDetail.curveStyle == QwtPlotCurve::Sticks) {

                QwtSymbol *sym = new QwtSymbol;
                sym->setSize(4);
                sym->setStyle(metricDetail.symbolStyle);
                sym->setPen(QPen(cd.color));
                sym->setBrush(QBrush(Qt::white));
                current->setSymbol(sym);

            }

            // add data labels
            if (metricDetail.labels) {

                QFont labelFont;
                labelFont.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
                labelFont.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

                // loop through each NONZERO value and add a label
                for  (int i=0; i<xdata.count(); i++) {

                    // we only want to do once per bar, which has 4 points
                    if (metricDetail.curveStyle == QwtPlotCurve::Steps && (i+1)%4) continue;

                    double value = metricDetail.curveStyle == QwtPlotCurve::Steps ? ydata[i-1] : ydata[i];

                    // bar headings always need to be centered
                    if (value) {

                        // format the label appropriately
                        const RideMetric *m = metricDetail.metric;
                        QString labelString;

                        if (m != NULL) {

                            // handle precision of 1 for seconds converted to hours
                            int precision = m->precision();
                            if (metricDetail.uunits == "seconds"  || metricDetail.uunits == tr("seconds")) precision=1;
                            if (metricDetail.uunits == "km") precision=0;

                            // we have a metric so lets be precise ...
                            labelString = QString("%1").arg(value, 0, 'f', precision);

                        } else {
                            // no precision
                            labelString = (QString("%1").arg(value, 0, 'f', 1));
                        }


                        // Qwt uses its own text objects
                        QwtText text(labelString);
                        text.setFont(labelFont);
                        text.setColor(cd.color);

                        // make that mark
                        QwtPlotMarker *label = new QwtPlotMarker();
                        label->setLabel(text);
                        label->setValue(xdata[i], ydata[i]);
                        label->setYAxis(axisid);
                        label->setSpacing(3); // not px but by yaxis value !? mad.

                        // Bars(steps) / sticks / dots: label above centered
                        // but bars have multiple points offset from their actual
                        // so need to adjust bars to centre above the top of the bar
                        if (metricDetail.curveStyle == QwtPlotCurve::Steps) {

                            // We only get every fourth point, so center
                            // between second and third point of bar "square"
                            label->setValue((xdata[i-1]+xdata[i-2])/2.00f, ydata[i-1]);
                        }

                        // Lables on a Line curve should be above/below depending upon the shape of the curve
                        if (metricDetail.curveStyle == QwtPlotCurve::Lines) {

                            label->setLabelAlignment(Qt::AlignTop | Qt::AlignCenter);

                            // we could simplify this into one if clause but it wouldn't be
                            // so obvious what we were doing
                            if (i && (i == ydata.count()-3) && ydata[i-1] > ydata[i]) {

                                // last point on curve
                                label->setLabelAlignment(Qt::AlignBottom | Qt::AlignCenter);

                            } else if (i && i < ydata.count()) {

                                // is a low / valley
                                if (ydata[i-1] > ydata[i] && ydata[i+1] > ydata[i])
                                    label->setLabelAlignment(Qt::AlignBottom | Qt::AlignCenter);

                            } else if (i == 0 && ydata[i+1] > ydata[i]) {

                                // first point on curve
                                label->setLabelAlignment(Qt::AlignBottom | Qt::AlignCenter);
                            }

                        } else {

                            label->setLabelAlignment(Qt::AlignTop | Qt::AlignCenter);
                        }

                        // and attach
                        label->attach(this);
                        labels << label;
                    }
                }
            }

            // smoothing
            if (metricDetail.smooth == true) {
                current->setCurveAttribute(QwtPlotCurve::Fitted, true);
            }

            // set the data series
            current->setSamples(xdata.data(),ydata.data(), count + 1);
            current->setBaseline(metricDetail.baseline);

            //qDebug()<<"Set Curve Data.."<<timer.elapsed();

            // update min/max Y values for the chosen axis
            if (current->maxYValue() > maxY[supportedAxes.indexOf(axisid)]) maxY[supportedAxes.indexOf(axisid)] = current->maxYValue();
            if (current->minYValue() < minY[supportedAxes.indexOf(axisid)]) minY[supportedAxes.indexOf(axisid)] = current->minYValue();

            current->attach(this);

        }

        // lastly set markers using the right color
        if (settings->groupBy != LTM_TOD) 
            refreshMarkers(settings, settings->start.date(), settings->end.date(), settings->groupBy, cd.color);

    }

    //qDebug()<<"Second plotting iteration.."<<timer.elapsed();

    // axes

    if (settings->groupBy != LTM_TOD) {

        int tics;
        if (MAXX < 14) {
            tics = 1;
        } else {
            tics = 1 + MAXX/10;
        }
        setAxisScale(xBottom, -0.498f, MAXX+0.498f, tics);
        setAxisScaleDraw(QwtPlot::xBottom, new CompareScaleDraw());


    } else {
        setAxisScale(xBottom, 0, 24, 2);
        setAxisScaleDraw(QwtPlot::xBottom, new LTMScaleDraw(settings->start,
                    groupForDate(settings->start.date(), settings->groupBy), settings->groupBy));
    }
    enableAxis(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xBottom, true);
    setAxisVisible(QwtAxis::xTop, false);

    // run through the Y axis
    for (int i=0; i<8; i++) {
        // set the scale on the axis
        if (i != xBottom && i != xTop) {
            maxY[i] *= 1.2; // add 20% headroom
            setAxisScale(supportedAxes[i], minY[i], maxY[i]);
        }
    }

    // if not stacked then lets make the yAxis a little
    // more descriptive and use the color of the curve
    if (set->metrics.count() == 1) {

        // title (units)
        QString units = set->metrics[0].uunits;
        QString name = set->metrics[0].uname;

        // abbreviate the coggan bullshit everyone loves    
        // but god only knows why (sheep?)
        if (name == "Coggan Acute Training Load") name = "ATL";
        if (name == "Coggan Chronic Training Load") name = "CTL";
        if (name == "Coggan Training Stress Balance") name = "TSB";

        QString title = name ;
        if (units != "" && units != name) title = title + " (" + units + ")";

        setAxisTitle(axisid, title);

        // color
        QPalette pal;
        pal.setColor(QPalette::WindowText, set->metrics[0].penColor);
        pal.setColor(QPalette::Text, set->metrics[0].penColor);
        axisWidget(axisid)->setPalette(pal);
    }

    QString format = axisTitle(yLeft).text();
    picker->setAxes(xBottom, yLeft);
    picker->setFormat(format);

    // show legend?
    if (settings->legend == false) this->legend()->hide();
    else this->legend()->show();

    QHashIterator<QString, QwtPlotCurve*> p(curves);
    while (p.hasNext()) {
        p.next();

        // always hide bollocksy curves
        if (p.key().endsWith(tr("trend")) || p.key().endsWith(tr("Outliers")) || p.key().endsWith(tr("Best")) || p.key().startsWith(tr("Best")))
            p.value()->setItemAttribute(QwtPlotItem::Legend, false);
        else
            p.value()->setItemAttribute(QwtPlotItem::Legend, settings->legend);
    }

    // now refresh
    updateLegend();

    // update colours etc for plot chrome
    configChanged(CONFIG_APPEARANCE);

    // plot
    replot();

    //qDebug()<<"Replot and done.."<<timer.elapsed();

}

int
LTMPlot::getMaxX()
{
    return MAXX;
}

void
LTMPlot::setMaxX(int x)
{
    MAXX = x;

    int tics;
    if (MAXX < 14) {
        tics = 1;
    } else {
        tics = 1 + MAXX/10;
    }
    setAxisScale(xBottom, -0.498f, MAXX+0.498f, tics);
    setAxisScaleDraw(QwtPlot::xBottom, new CompareScaleDraw());
}

void
LTMPlot::createTODCurveData(Context *context, LTMSettings *settings, MetricDetail metricDetail, QVector<double>&x,QVector<double>&y,int&n,bool)
{
    y.clear();
    x.clear();

    x.resize((24+3));
    y.resize((24+3));
    n = (24);

    for (int i=0; i<(24); i++) x[i]=i;

    foreach (RideItem *ride, context->athlete->rideCache->rides()) {

        if (!settings->specification.pass(ride)) continue;

        double value = ride->getForSymbol(metricDetail.symbol);

        // check values are bounded to stop QWT going berserk
        if (std::isnan(value) || std::isinf(value)) value = 0;

        // Special computed metrics (LTS/STS) have a null metric pointer
        if (metricDetail.metric) {
            // convert from stored metric value to imperial
            if (context->athlete->useMetricUnits == false) {
                value *= metricDetail.metric->conversion();
                value += metricDetail.metric->conversionSum();
            }

            // convert seconds to hours
            if (metricDetail.metric->units(true) == "seconds" ||
                metricDetail.metric->units(true) == tr("seconds")) value /= 3600;
        }

        int array = ride->dateTime.time().hour();
        int type = metricDetail.metric ? metricDetail.metric->type() : RideMetric::Average;
        bool aggZero = metricDetail.metric ? metricDetail.metric->aggregateZero() : false;

        // set aggZero to false and value to zero if is temperature and -255
        if (metricDetail.metric && metricDetail.metric->symbol() == "average_temp" && value == RideFile::NoTemp) {
            value = 0;
            aggZero = false;
        }

        if (metricDetail.uunits == "Ramp" ||
            metricDetail.uunits == tr("Ramp")) type = RideMetric::Total;

        switch (type) {
        case RideMetric::Total:
            y[array] += value;
            break;
        case RideMetric::Average:
            {
            // average should be calculated taking into account
            // the duration of the ride, otherwise high value but
            // short rides will skew the overall average
            if (value || aggZero)
                y[array] = value; //XXX average is broken
            break;
            }
        case RideMetric::Low:
            if (value < y[array]) y[array] = value;
            break;
        case RideMetric::Peak:
            if (value > y[array]) y[array] = value;
            break;
        }
    }
}

void
LTMPlot::createCurveData(Context *context, LTMSettings *settings, MetricDetail metricDetail, QVector<double>&x,QVector<double>&y,int&n, bool forceZero)
{
    // create curves depending on type ...
    if (metricDetail.type == METRIC_DB || metricDetail.type == METRIC_META) {
        createMetricData(context, settings, metricDetail, x,y,n, forceZero);
        return;
    } else if (metricDetail.type == METRIC_STRESS || metricDetail.type == METRIC_PM) {
        createPMCData(context, settings, metricDetail, x,y,n, forceZero);
        return;
    } else if (metricDetail.type == METRIC_BEST) {
        createBestsData(context,settings,metricDetail,x,y,n, forceZero);
        return;
    } else if (metricDetail.type == METRIC_ESTIMATE) {
        createEstimateData(context, settings, metricDetail, x,y,n, forceZero);
        return;
    }

}

void
LTMPlot::createMetricData(Context *context, LTMSettings *settings, MetricDetail metricDetail,
                                              QVector<double>&x,QVector<double>&y,int&n, bool forceZero)
{

    // resize the curve array to maximum possible size
    int maxdays = groupForDate(settings->end.date(), settings->groupBy)
                    - groupForDate(settings->start.date(), settings->groupBy);

    x.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail
    y.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail

    // do we aggregate ?
    bool aggZero = metricDetail.metric ? metricDetail.metric->aggregateZero() : false;

    n=-1;
    int lastDay=0;
    unsigned long secondsPerGroupBy=0;
    bool wantZero = forceZero ? 1 : (metricDetail.curveStyle == QwtPlotCurve::Steps);

    foreach (RideItem *ride, context->athlete->rideCache->rides()) { 

        // filter out unwanted stuff
        if (!settings->specification.pass(ride)) continue;

        // day we are on
        int currentDay = groupForDate(ride->dateTime.date(), settings->groupBy);

        // value for day
        double value;
        if (metricDetail.type == METRIC_META)
            value = ride->getText(metricDetail.symbol, "0.0").toDouble();
        else
            value = ride->getForSymbol(metricDetail.symbol);

        // check values are bounded to stop QWT going berserk
        if (std::isnan(value) || std::isinf(value)) value = 0;

        // set aggZero to false and value to zero if is temperature and -255
        if (metricDetail.metric && metricDetail.metric->symbol() == "average_temp" && value == RideFile::NoTemp) {
            value = 0;
            aggZero = false;
        }

        if (metricDetail.metric) {
            // convert from stored metric value to imperial
            if (context->athlete->useMetricUnits == false) {
                value *= metricDetail.metric->conversion();
                value += metricDetail.metric->conversionSum();
            }

            // convert seconds to hours
            if (metricDetail.metric->units(true) == "seconds" ||
                metricDetail.metric->units(true) == tr("seconds")) value /= 3600;
        }

        if (value || wantZero) {
            unsigned long seconds = ride->getForSymbol("workout_time");
            if (currentDay > lastDay) {
                if (lastDay && wantZero) {
                    while (lastDay<currentDay) {
                        lastDay++;
                        n++;
                        x[n]=lastDay - groupForDate(settings->start.date(), settings->groupBy);
                        y[n]=0;
                    }
                } else {
                    n++;
                }

                // first time thru
                if (n<0) n=0;

                y[n] = value;
                x[n] = currentDay - groupForDate(settings->start.date(), settings->groupBy);

                // only increment counter if nonzero or we aggregate zeroes
                if (value || aggZero) secondsPerGroupBy = seconds; 

            } else {
                // sum totals, average averages and choose best for Peaks
                int type = metricDetail.metric ? metricDetail.metric->type() : RideMetric::Average;

                if (metricDetail.uunits == "Ramp" ||
                    metricDetail.uunits == tr("Ramp")) type = RideMetric::Total;

                if (metricDetail.type == METRIC_BEST) type = RideMetric::Peak;

                // first time thru
                if (n<0) n=0;

                switch (type) {
                case RideMetric::Total:
                    y[n] += value;
                    break;
                case RideMetric::Average:
                    {
                    // average should be calculated taking into account
                    // the duration of the ride, otherwise high value but
                    // short rides will skew the overall average
                    if (value || aggZero) y[n] = ((y[n]*secondsPerGroupBy)+(seconds*value)) / (secondsPerGroupBy+seconds);
                    break;
                    }
                case RideMetric::Low:
                    if (value < y[n]) y[n] = value;
                    break;
                case RideMetric::Peak:
                    if (value > y[n]) y[n] = value;
                    break;
                }
                secondsPerGroupBy += seconds; // increment for same group
            }
            lastDay = currentDay;
        }
    }
}

void
LTMPlot::createBestsData(Context *, LTMSettings *settings, MetricDetail metricDetail, QVector<double>&x,QVector<double>&y,int&n, bool forceZero)
{
    // resize the curve array to maximum possible size
    int maxdays = groupForDate(settings->end.date(), settings->groupBy)
                    - groupForDate(settings->start.date(), settings->groupBy);

    x.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail
    y.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail

    // do we aggregate ?
    bool aggZero = metricDetail.metric ? metricDetail.metric->aggregateZero() : false;

    n=-1;
    int lastDay=0;
    unsigned long secondsPerGroupBy=0;
    bool wantZero = forceZero ? 1 : (metricDetail.curveStyle == QwtPlotCurve::Steps);

    foreach (RideBest best, *(settings->bests)) { 

        // filter has already been applied

        // day we are on
        int currentDay = groupForDate(best.getRideDate().date(), settings->groupBy);

        // value for day
        double value;
        value = best.getForSymbol(metricDetail.bestSymbol);

        // check values are bounded to stop QWT going berserk
        if (std::isnan(value) || std::isinf(value)) value = 0;

        // set aggZero to false and value to zero if is temperature and -255
        if (metricDetail.metric && metricDetail.metric->symbol() == "average_temp" && value == RideFile::NoTemp) {
            value = 0;
            aggZero = false;
        }

        if (value || wantZero) {
            unsigned long seconds = 1;
            if (currentDay > lastDay) {
                if (lastDay && wantZero) {
                    while (lastDay<currentDay) {
                        lastDay++;
                        n++;
                        x[n]=lastDay - groupForDate(settings->start.date(), settings->groupBy);
                        y[n]=0;
                    }
                } else {
                    n++;
                }

                y[n] = value;
                x[n] = currentDay - groupForDate(settings->start.date(), settings->groupBy);

                // only increment counter if nonzero or we aggregate zeroes
                if (value || aggZero) secondsPerGroupBy = seconds; 

            } else {
                // sum totals, average averages and choose best for Peaks
                int type = metricDetail.metric ? metricDetail.metric->type() : RideMetric::Average;

                if (metricDetail.uunits == "Ramp" ||
                    metricDetail.uunits == tr("Ramp")) type = RideMetric::Total;

                if (metricDetail.type == METRIC_BEST) type = RideMetric::Peak;

                // first time thru
                //if (n<0) n++;

                switch (type) {
                case RideMetric::Total:
                    y[n] += value;
                    break;
                case RideMetric::Average:
                    {
                    // average should be calculated taking into account
                    // the duration of the ride, otherwise high value but
                    // short rides will skew the overall average
                    if (value || aggZero) y[n] = ((y[n]*secondsPerGroupBy)+(seconds*value)) / (secondsPerGroupBy+seconds);
                    break;
                    }
                case RideMetric::Low:
                    if (value < y[n]) y[n] = value;
                    break;
                case RideMetric::Peak:
                    if (value > y[n]) y[n] = value;
                    break;
                }
                secondsPerGroupBy += seconds; // increment for same group
            }
            lastDay = currentDay;
        }
    }
}

void
LTMPlot::createEstimateData(Context *context, LTMSettings *settings, MetricDetail metricDetail,
                                              QVector<double>&x,QVector<double>&y,int&n, bool)
{
    // lets refresh the model data if we don't have any
    if (context->athlete->PDEstimates.count() == 0) context->athlete->rideCache->refreshCPModelMetrics(); 

    // resize the curve array to maximum possible size (even if we don't need it)
    int maxdays = groupForDate(settings->end.date(), settings->groupBy)
                    - groupForDate(settings->start.date(), settings->groupBy);

    n = 0;
    x.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail
    y.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail

    // what is the first date
    int firstDay = groupForDate(settings->start.date(), settings->groupBy);

    // loop through all the estimate data
    foreach(PDEstimate est, context->athlete->PDEstimates) {

        // wpk skip for now
        if (est.wpk != metricDetail.wpk) continue;

        // skip entries for other models
        if (est.model != metricDetail.model) continue;

        // skip if no in our time period
        if (est.to < settings->start.date() || est.from > settings->end.date()) continue;

        // get dat for first and last
        QDate from = est.from < settings->start.date() ? settings->start.date() : est.from;
        QDate to = est.to > settings->end.date() ? settings->end.date() : est.to;

        // what value to plot ?
        double value=0;

        switch(metricDetail.estimate) {
        case ESTIMATE_WPRIME :
            value = est.WPrime;
            break;

        case ESTIMATE_CP :
            value = est.CP;
            break;

        case ESTIMATE_FTP :
            value = est.FTP;
            break;

        case ESTIMATE_PMAX :
            value = est.PMax;
            break;

        case ESTIMATE_BEST :
            {
                value = 0;

                // we need to find the model 
                foreach(PDModel *model, models) {

                    // not the one we want
                    if (model->code() != metricDetail.model) continue;

                    // set the parameters previously derived
                    model->loadParameters(est.parameters);

                    // get the model estimate for our duration
                    value = model->y(metricDetail.estimateDuration * metricDetail.estimateDuration_units);
                }
            }
            break;

        case ESTIMATE_EI :
            value = est.EI;
            break;
        }

        if (n <= maxdays && value > 0) {
            int currentDay = groupForDate(from, settings->groupBy);
            x[n] = currentDay - firstDay;
            y[n] = value;
            n++;

            int nextDay = groupForDate(to, settings->groupBy);
            while (n <= maxdays && nextDay > currentDay) { // i.e. not the same day
                x[n] = 1 + currentDay - firstDay;
                y[n] = value;
                n++;
                currentDay++;
            }
        }
    }

    // always seems to be one too many ...
    if (n>0)n--;
}

void
LTMPlot::createPMCData(Context *context, LTMSettings *settings, MetricDetail metricDetail,
                                              QVector<double>&x,QVector<double>&y,int&n, bool)
{
    QString scoreType;
    int stressType = STRESS_LTS;

    // create a custom set of summary metric data!
    if (metricDetail.type == METRIC_PM) {

        if (metricDetail.symbol.startsWith("skiba")) {
            scoreType = "skiba_bike_score";
        } else if (metricDetail.symbol.startsWith("antiss")) {
            scoreType = "antiss_score";
        } else if (metricDetail.symbol.startsWith("atiss")) {
            scoreType = "atiss_score";
        } else if (metricDetail.symbol.startsWith("coggan")) {
            scoreType = "coggan_tss";
        } else if (metricDetail.symbol.startsWith("daniels")) {
            scoreType = "daniels_points";
        } else if (metricDetail.symbol.startsWith("trimp")) {
            scoreType = "trimp_points";
        } else if (metricDetail.symbol.startsWith("work")) {
            scoreType = "total_work";
        } else if (metricDetail.symbol.startsWith("cp_")) {
            scoreType = "skiba_cp_exp";
        } else if (metricDetail.symbol.startsWith("wprime")) {
            scoreType = "skiba_wprime_exp";
        } else if (metricDetail.symbol.startsWith("distance")) {
            scoreType = "total_distance";
        } else if (metricDetail.symbol.startsWith("govss")) {
            scoreType = "govss";
        }

        stressType = STRESS_LTS; // if in doubt
        if (metricDetail.symbol.endsWith("lts") || metricDetail.symbol.endsWith("ctl")) 
            stressType = STRESS_LTS;
        else if (metricDetail.symbol.endsWith("sts") || metricDetail.symbol.endsWith("atl")) 
            stressType = STRESS_STS;
        else if (metricDetail.symbol.endsWith("sb")) 
            stressType = STRESS_SB;
        else if (metricDetail.symbol.endsWith("lr")) 
            stressType = STRESS_RR;

    } else {

        scoreType = metricDetail.symbol; // just use the selected metric
        stressType = metricDetail.stressType;
    }


    PMCData *athletePMC = NULL;
    PMCData *localPMC = NULL;

    // create local PMC if filtered
    if (settings->specification.isFiltered()) {

        // don't filter for date range!!
        Specification allDates = settings->specification;
        allDates.setDateRange(DateRange(QDate(),QDate()));
        localPMC = new PMCData(context, allDates, scoreType);
    }

    // use global one if not filtered
    if (!localPMC) athletePMC = context->athlete->getPMCFor(scoreType);

    // point to the right one
    PMCData *pmcData = localPMC ? localPMC : athletePMC;

    int maxdays = groupForDate(settings->end.date(), settings->groupBy)
                    - groupForDate(settings->start.date(), settings->groupBy);

    // skip for negative or empty time periods.
    if (maxdays <=0) return;

    x.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail
    y.resize(maxdays+3); // one for start from zero plus two for 0 value added at head and tail

    // iterate over it and create curve...
    n=-1;
    int lastDay=0;
    unsigned long secondsPerGroupBy=0;
    bool wantZero = true;

    for (QDate date=settings->start.date(); date <= settings->end.date(); date = date.addDays(1)) {

        // day we are on
        int currentDay = groupForDate(date, settings->groupBy);

        // value for day
        double value = 0.0f;

        switch (stressType) {
        case STRESS_LTS:
            value = pmcData->lts(date);
            break;
        case STRESS_STS:
            value = pmcData->sts(date);
            break;
        case STRESS_SB:
            value = pmcData->sb(date);
            break;
        case STRESS_RR:
            value = pmcData->rr(date);
            break;
        default:
            value = 0;
            break;
        }
        
        if (value || wantZero) {
            unsigned long seconds = 1;
            if (currentDay > lastDay) {
                if (lastDay && wantZero) {
                    while (lastDay<currentDay) {
                        lastDay++;
                        n++;
                        x[n]=lastDay - groupForDate(settings->start.date(), settings->groupBy);
                        y[n]=0;
                    }
                } else {
                    n++;
                }

                y[n] = value;
                x[n] = currentDay - groupForDate(settings->start.date(), settings->groupBy);

                // only increment counter if nonzero or we aggregate zeroes
                secondsPerGroupBy = seconds; 

            } else {
                // sum totals, average averages and choose best for Peaks
                int type = RideMetric::Average;

                if (metricDetail.uunits == "Ramp" ||
                    metricDetail.uunits == tr("Ramp")) type = RideMetric::Total;

                // first time thru
                if (n<0) n++;

                switch (type) {
                case RideMetric::Total:
                    y[n] += value;
                    break;
                case RideMetric::Average:
                    {
                    // average should be calculated taking into account
                    // the duration of the ride, otherwise high value but
                    // short rides will skew the overall average
                    y[n] = ((y[n]*secondsPerGroupBy)+(seconds*value)) / (secondsPerGroupBy+seconds);
                    break;
                    }
                case RideMetric::Low:
                    if (value < y[n]) y[n] = value;
                    break;
                case RideMetric::Peak:
                    if (value > y[n]) y[n] = value;
                    break;
                }
                secondsPerGroupBy += seconds; // increment for same group
            }
            lastDay = currentDay;
        }
    }

    // wipe away local
    if (localPMC) delete localPMC;
}

QwtAxisId
LTMPlot::chooseYAxis(QString units)
{
    QwtAxisId chosen(-1,-1);
    // return the YAxis to use
    if ((chosen = axes.value(units, QwtAxisId(-1,-1))) != QwtAxisId(-1,-1)) return chosen;
    else if (axes.count() < 8) {
        chosen = supportedAxes[axes.count()];
        if (units == "seconds" || units == tr("seconds")) setAxisTitle(chosen, tr("hours")); // we convert seconds to hours
        else setAxisTitle(chosen, units);
        enableAxis(chosen.id, true);
        setAxisVisible(chosen, true);
        axes.insert(units, chosen);
        return chosen;

    } else {
        // eek!
        return QwtAxis::yLeft; // just re-use the current yLeft axis
    }
}

bool
LTMPlot::eventFilter(QObject *obj, QEvent *event)
{

    // when clicking on a legend item, toggle if the curve is visible
    if (obj == legend() && event->type() == QEvent::MouseButtonPress) {

        bool replotNeeded = false;
        QwtLegend *l = static_cast<QwtLegend *>(this->legend());
        QPoint pos = QCursor::pos();

        foreach(QwtPlotCurve *p, curves) {
            foreach (QWidget *w, l->legendWidgets(itemToInfo(p))) {
                if (QRect(l->mapToGlobal(w->geometry().topLeft()),
                    l->mapToGlobal(w->geometry().bottomRight())).contains(pos)) {

                    //qDebug()<<"under mouse="<<static_cast<QwtLegendLabel*>(w)->text().text();
                    for(int m=0; m< settings->metrics.count(); m++) {
                        if (settings->metrics[m].curve == p) {
                            settings->metrics[m].hidden = !settings->metrics[m].hidden;
                            replotNeeded = true;
                        }
                    }
                }
            }
        }

        if (replotNeeded) setData(settings);
    }

    // is it for other objects ?
    if (axesObject.contains(obj)) {

        QwtAxisId id = axesId.at(axesObject.indexOf(obj));

        // this is an axes widget
        //qDebug()<<this<<"event on="<<id<< static_cast<QwtScaleWidget*>(obj)->title().text() <<"event="<<event->type();

        // isolate / restore on mouse enter leave
        if (!isolation && event->type() == QEvent::Enter) {

            // isolate curve on hover
            curveColors->isolateAxis(id);
            replot();

        } else if (!isolation && event->type() == QEvent::Leave) {

            // return to normal when leave
            curveColors->restoreState();
            replot();

        } else if (event->type() == QEvent::MouseButtonRelease) {

            // click on any axis to toggle isolation
            // if isolation is on, just turns it off
            // if isolation is off, turns it on for the axis clicked
            if (isolation) {
                isolation = false;
                curveColors->restoreState();
                replot();
            } else {
                isolation = true;
                curveColors->isolateAxis(id, true); // with scale adjust
                replot();
            }
        }
    }

    return false;
}

int
LTMPlot::groupForDate(QDate date, int groupby)
{
    switch(groupby) {
    case LTM_WEEK:
        {
        // must start from 1 not zero!
        return 1 + ((date.toJulianDay() - settings->start.date().toJulianDay()) / 7);
        }
    case LTM_MONTH: return (date.year()*12) + date.month();
    case LTM_YEAR:  return date.year();
    case LTM_DAY:
    default:
        return date.toJulianDay();
    case LTM_ALL: return 1;

    }
}

void
LTMPlot::pointHover(QwtPlotCurve *curve, int index)
{
    if (index >= 0 && curve != highlighter) {

        int stacknum = stacks.value(curve, -1);

        const RideMetricFactory &factory = RideMetricFactory::instance();
        double value;
        QString units;
        int precision = 0;
        QString datestr;

        if (!parent->isCompare()) {
            LTMScaleDraw *lsd = new LTMScaleDraw(settings->start, groupForDate(settings->start.date(), settings->groupBy), settings->groupBy);
            QwtText startText = lsd->label((int)(curve->sample(index).x()+0.5));

            if (settings->groupBy == LTM_ALL)
                datestr = QString(tr("All"));
            else if (settings->groupBy != LTM_WEEK)
                datestr = startText.text();
            else
                datestr = QString(tr("Week Commencing %1")).arg(startText.text());

            datestr = datestr.replace('\n', ' ');
        }

        // we reference the metric definitions of name and
        // units to decide on the level of precision required
        QHashIterator<QString, QwtPlotCurve*> c(curves);
        while (c.hasNext()) {
            c.next();
            if (c.value() == curve) {
                const RideMetric *metric =factory.rideMetric(c.key());
                units = metric ? metric->units(context->athlete->useMetricUnits) : "";
                precision = metric ? metric->precision() : 1;

                // BikeScore, RI and Daniels Points have no units
                if (units == "" && metric != NULL) {
                    QTextEdit processHTML(factory.rideMetric(c.key())->name());
                    units  = processHTML.toPlainText();
                }
                break;
            }
        }

        // the point value
        value = curve->sample(index).y();

        // de-aggregate stacked values
        if (stacknum > 0) {
            value = stackY[stacknum]->at(index) - stackY[stacknum-1]->at(index); // de-aggregate
        }

        // convert seconds to hours for the LTM plot
        if (units == "seconds" || units == tr("seconds")) {
            units = "hours"; // we translate from seconds to hours
            value = ceil(value*10.0)/10.0;
            precision = 1; // need more precision now
        }

        // output the tooltip
        QString text;
        if (!parent->isCompare()) {
            text = QString("%1\n%2\n%3 %4")
                        .arg(datestr)
                        .arg(curve->title().text())
                        .arg(value, 0, 'f', precision)
                        .arg(this->axisTitle(curve->yAxis()).text());
        } else {
            text = QString("%1\n%2 %3")
                        .arg(curve->title().text())
                        .arg(value, 0, 'f', precision)
                        .arg(this->axisTitle(curve->yAxis()).text());
        }

        // set that text up
        picker->setText(text);
    } else {
        // no point
        picker->setText("");
    }
}

void
LTMPlot::pointClicked(QwtPlotCurve *curve, int index)
{
    // do nothing on a compare chart
    if (parent->isCompare()) return;

    if (index >= 0 && curve != highlighter) {
        // setup the popup
        parent->pointClicked(curve, index);
    }
}

// aggregate curve data, adds w to a and
// updates a directly. arrays MUST be of
// equal dimensions
void
LTMPlot::aggregateCurves(QVector<double> &a, QVector<double>&w)
{
    if (a.size() != w.size()) return; // ignore silently

    // add them in!
    for(int i=0; i<a.size(); i++) a[i] += w[i];
}

/*----------------------------------------------------------------------
 * Draw Power Zone Shading on Background (here to end of source file)
 *
 * THANKS TO DAMIEN GRAUSER FOR GETTING THIS WORKING TO SHOW
 * ZONE SHADING OVER TIME. WHEN CP CHANGES THE ZONE SHADING AND
 * LABELLING CHANGES TOO. NEAT.
 *--------------------------------------------------------------------*/
class LTMPlotBackground: public QwtPlotItem
{
    private:
        LTMPlot *parent;

    public:

        LTMPlotBackground(LTMPlot *_parent, QwtAxisId axisid)
        {
            //setAxis(QwtPlot::xBottom, axisid);
            setXAxis(axisid);
            setZ(0.0);
            parent = _parent;
        }

    virtual int rtti() const
    {
        return QwtPlotItem::Rtti_PlotUserItem;
    }

    virtual void draw(QPainter *painter,
                      const QwtScaleMap &xMap, const QwtScaleMap &yMap,
                      const QRectF &rect) const
    {
        const Zones *zones       = parent->parent->context->athlete->zones();
        int zone_range_size     = parent->parent->context->athlete->zones()->getRangeSize();

        if (zone_range_size >= 0) { //parent->shadeZones() &&
            for (int i = 0; i < zone_range_size; i ++) {
            int zone_range = i;
            int left = xMap.transform(parent->groupForDate(zones->getStartDate(zone_range), parent->settings->groupBy)- parent->groupForDate(parent->settings->start.date(), parent->settings->groupBy));

            /* The +50 pixels is for a QWT bug? cover the little gap on the right? */
            int right = xMap.transform(parent->maxX + 0.5) + 50;

            if (right<0)
                right= xMap.transform(parent->groupForDate(parent->settings->end.date(), parent->settings->groupBy) - parent->groupForDate(parent->settings->start.date(), parent->settings->groupBy));

            QList <int> zone_lows = zones->getZoneLows(zone_range);
            int num_zones = zone_lows.size();
            if (num_zones > 0) {
                for (int z = 0; z < num_zones; z ++) {
                    QRectF r = rect;
                    r.setLeft(left);
                    r.setRight(right);

                    QColor shading_color = zoneColor(z, num_zones);
                    shading_color.setHsv(
                        shading_color.hue(),
                        shading_color.saturation() / 4,
                        shading_color.value()
                        );
                    r.setBottom(yMap.transform(zone_lows[z]));
                    if (z + 1 < num_zones)
                        r.setTop(yMap.transform(zone_lows[z + 1]));
                    if (r.top() <= r.bottom())
                        painter->fillRect(r, shading_color);
                }
            }
            }
        }
    }
};


// Zone labels are drawn if power zone bands are enabled, automatically
// at the center of the plot
class LTMPlotZoneLabel: public QwtPlotItem
{
    private:
        LTMPlot *parent;
        int zone_number;
        double watts;
        QwtText text;

    public:
        LTMPlotZoneLabel(LTMPlot *_parent, int _zone_number, QwtAxisId axisid, LTMSettings *settings)
        {
            parent = _parent;
            zone_number = _zone_number;

            const Zones *zones       = parent->parent->context->athlete->zones();
            int zone_range     = zones->whichRange(settings->start.addDays((settings->end.date().toJulianDay()-settings->start.date().toJulianDay())/2).date());

            // which axis has watts?
            setXAxis(axisid);

            // create new zone labels if we're shading
            if (zone_range >= 0) { //parent->shadeZones()
                QList <int> zone_lows = zones->getZoneLows(zone_range);
                QList <QString> zone_names = zones->getZoneNames(zone_range);
                int num_zones = zone_lows.size();
                if (zone_names.size() != num_zones) return;
                if (zone_number < num_zones) {
                    watts =
                        (
                            (zone_number + 1 < num_zones) ?
                            0.5 * (zone_lows[zone_number] + zone_lows[zone_number + 1]) :
                            (
                                (zone_number > 0) ?
                                (1.5 * zone_lows[zone_number] - 0.5 * zone_lows[zone_number - 1]) :
                                2.0 * zone_lows[zone_number]
                            )
                        );
                    text = QwtText(zone_names[zone_number]);
                    text.setFont(QFont("Helvetica",20, QFont::Bold));
                    QColor text_color = zoneColor(zone_number, num_zones);
                    text_color.setAlpha(64);
                    text.setColor(text_color);
                }
            }
            setZ(1.0 + zone_number / 100.0);
        }

        virtual int rtti() const
        {
            return QwtPlotItem::Rtti_PlotUserItem;
        }

        void draw(QPainter *painter,
                  const QwtScaleMap &, const QwtScaleMap &yMap,
                  const QRectF &rect) const
        {
            if (true) {//parent->shadeZones()
                int x = (rect.left() + rect.right()) / 2;
                int y = yMap.transform(watts);

                // the following code based on source for QwtPlotMarker::draw()
                QRect tr(QPoint(0, 0), text.textSize(painter->font()).toSize());
                tr.moveCenter(QPoint(x, y));
                text.draw(painter, tr);
            }
        }
};

void
LTMPlot::refreshMarkers(LTMSettings *settings, QDate from, QDate to, int groupby, QColor color)
{
    double baseday = groupForDate(from, groupby);

    // seasons and season events
    if (settings->events) {

        foreach (Season s, context->athlete->seasons->seasons) {

            if (s.type != Season::temporary && s.name != settings->title && s.getStart() >= from && s.getStart() < to) {

                QwtPlotMarker *mrk = new QwtPlotMarker;
                markers.append(mrk);
                mrk->attach(this);
                mrk->setLineStyle(QwtPlotMarker::VLine);
                mrk->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
                mrk->setLinePen(QPen(color, 0, Qt::DashLine));
                mrk->setValue(double(groupForDate(s.getStart(), groupby)) - baseday, 0.0);

                if (first) {
                    QwtText text(s.getName());
                    text.setFont(QFont("Helvetica", 10, QFont::Bold));
                    text.setColor(color);
                    mrk->setLabel(text);
                }
            }

            foreach (SeasonEvent event, s.events) {


                if (event.date > from && event.date < to) {

                    // and the events...
                    QwtPlotMarker *mrk = new QwtPlotMarker;
                    markers.append(mrk);
                    mrk->attach(this);
                    mrk->setLineStyle(QwtPlotMarker::VLine);
                    mrk->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
                    mrk->setLinePen(QPen(color, 0, Qt::SolidLine));
                    mrk->setValue(double(groupForDate(event.date, groupby)) - baseday, 10.0);

                    if (first) {
                        QwtText text(event.name);
                        text.setFont(QFont("Helvetica", 10, QFont::Bold));
                        text.setColor(Qt::red);
                        mrk->setLabel(text);
                    }

                }
            }
        }
    }
    return;
}

void
LTMPlot::refreshZoneLabels(QwtAxisId axisid)
{
    foreach(LTMPlotZoneLabel *label, zoneLabels) {
        label->detach();
        delete label;
    }
    zoneLabels.clear();

    if (bg) {
        bg->detach();
        delete bg;
        bg = NULL;
    }
    if (axisid == QwtAxisId(-1,-1)) return; // our job is done - no zones to plot

    const Zones *zones       = context->athlete->zones();

    if (zones == NULL || zones->getRangeSize()==0) return; // no zones to plot

    int zone_range     = 0; // first range

    // generate labels for existing zones
    if (zone_range >= 0) {
        int num_zones = zones->numZones(zone_range);
        for (int z = 0; z < num_zones; z ++) {
            LTMPlotZoneLabel *label = new LTMPlotZoneLabel(this, z, axisid, settings);
            label->attach(this);
            zoneLabels.append(label);
        }
    }
    bg = new LTMPlotBackground(this, axisid);
    bg->attach(this);
}
