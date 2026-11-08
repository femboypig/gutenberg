package org.gutenberg.conformance;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

public final class DrawingView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private float x = 60, y = 60;
    private final float radius;

    public DrawingView(Context context, AttributeSet attrs) {
        super(context, attrs);
        TypedArray values = context.obtainStyledAttributes(attrs, R.styleable.DrawingView);
        radius = values.getDimension(R.styleable.DrawingView_circleRadius, 24);
        if (radius != 26 || values.getIndexCount() != 1) throw new IllegalStateException("custom XML attributes");
        values.recycle();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.drawColor(0xffe3f2fd);
        paint.setColor(0xff1565c0);
        paint.setStyle(Paint.Style.FILL);
        canvas.drawCircle(x, y, radius, paint);
        int saved = canvas.save();
        canvas.translate(120, 24);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        path.reset();
        path.moveTo(0, 0);
        path.quadTo(50, 80, 90, 0);
        canvas.drawPath(path, paint);
        canvas.restoreToCount(saved);
        paint.setStyle(Paint.Style.FILL);
        paint.setTextSize(14);
        canvas.drawText("Canvas " + getWidth() + " × " + getHeight(), 12, 150, paint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN || event.getActionMasked() == MotionEvent.ACTION_MOVE) {
            x = event.getX();
            y = event.getY();
            invalidate();
        }
        return true;
    }
}
