#pragma once
#include <QLabel>
#include <QPainter>

// QLabel's text-based minimum size can prevent an entire splitter pane from
// shrinking. Paint within the allocated space instead of imposing that size.
class FitTextLabel : public QLabel {
public:
    using QLabel::QLabel;
    QSize sizeHint() const override { return {1, 1}; }
    QSize minimumSizeHint() const override { return {1, 1}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        QFont fitted = font();
        const QRect area = contentsRect();
        for (int px = 13; px >= 6; --px) {
            fitted.setPixelSize(px);
            const QFontMetrics metrics(fitted);
            if (metrics.horizontalAdvance(text()) <= area.width() && metrics.height() <= area.height()) break;
        }
        painter.setFont(fitted);
        painter.setPen(palette().color(foregroundRole()));
        painter.setClipRect(area);
        painter.drawText(area, alignment() | Qt::TextSingleLine, text());
    }
};
