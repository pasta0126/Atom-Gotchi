import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../models/gotchi_state.dart';

/// Replica las expresiones del dispositivo en la pantalla del teléfono
class GotchiFace extends StatelessWidget {
  final GotchiMood mood;
  final double size;

  const GotchiFace({super.key, required this.mood, this.size = 260});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: size,
      height: size,
      child: CustomPaint(painter: _FacePainter(mood)),
    );
  }
}

class _FacePainter extends CustomPainter {
  final GotchiMood mood;
  const _FacePainter(this.mood);

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height * 0.45;
    final fr = size.width * 0.38;

    // Fondo
    canvas.drawRect(Rect.fromLTWH(0, 0, size.width, size.height),
        Paint()..color = _bgColor());

    // Cara
    canvas.drawCircle(Offset(cx, cy), fr, Paint()..color = _faceColor());
    canvas.drawCircle(Offset(cx, cy), fr,
        Paint()
          ..color = Colors.black
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2);

    // Ojos y boca
    final lex = cx - fr * 0.43;
    final rex = cx + fr * 0.43;
    final ey  = cy - fr * 0.18;
    final my  = cy + fr * 0.35;

    _drawExpression(canvas, lex, rex, ey, my, cx, cy, fr);
  }

  void _drawExpression(Canvas canvas,
      double lex, double rex, double ey, double my,
      double cx, double cy, double fr) {
    switch (mood) {
      case GotchiMood.happy:
        _eyeOpen(canvas, lex, ey, fr * 0.17);
        _eyeOpen(canvas, rex, ey, fr * 0.17);
        _mouthSmile(canvas, cx, my, fr * 0.35);
      case GotchiMood.tired:
        _eyeHalf(canvas, lex, ey, fr * 0.17);
        _eyeHalf(canvas, rex, ey, fr * 0.17);
        _mouthFrown(canvas, cx, my, fr * 0.28);
      case GotchiMood.hungry:
        _eyeOpen(canvas, lex, ey, fr * 0.17);
        _eyeOpen(canvas, rex, ey, fr * 0.17);
        _mouthWavy(canvas, cx, my, fr * 0.35);
      case GotchiMood.thirsty:
        _eyeOpen(canvas, lex, ey, fr * 0.17);
        _eyeOpen(canvas, rex, ey, fr * 0.17);
        _mouthTongue(canvas, cx, my, fr * 0.28);
      case GotchiMood.dizzy:
        _eyeX(canvas, lex, ey, fr * 0.18);
        _eyeX(canvas, rex, ey, fr * 0.18);
        _mouthFrown(canvas, cx, my, fr * 0.28);
      case GotchiMood.excited:
        _eyeStar(canvas, lex, ey, fr * 0.18);
        _eyeStar(canvas, rex, ey, fr * 0.18);
        _mouthOpen(canvas, cx, my, fr * 0.22, fr * 0.17);
      case GotchiMood.scared:
        _eyeWide(canvas, lex, ey, fr * 0.21);
        _eyeWide(canvas, rex, ey, fr * 0.21);
        _mouthOpen(canvas, cx, my + fr * 0.05, fr * 0.20, fr * 0.15);
        _drawSweat(canvas, cx + fr * 0.7, cy - fr * 0.5, fr * 0.08);
      case GotchiMood.laughing:
        _eyeHappy(canvas, lex, ey, fr * 0.18);
        _eyeHappy(canvas, rex, ey, fr * 0.18);
        _mouthGrin(canvas, cx, my, fr * 0.38, fr * 0.15);
      case GotchiMood.angry:
        _eyeAngry(canvas, lex, ey, fr * 0.17, left: true);
        _eyeAngry(canvas, rex, ey, fr * 0.17, left: false);
        _mouthFrown(canvas, cx, my, fr * 0.30);
      case GotchiMood.sad:
        _eyeOpen(canvas, lex, ey, fr * 0.17);
        _eyeOpen(canvas, rex, ey, fr * 0.17);
        _mouthFrown(canvas, cx, my, fr * 0.30);
        _drawTear(canvas, lex + fr * 0.05, ey + fr * 0.28, fr * 0.06);
        _drawTear(canvas, rex + fr * 0.05, ey + fr * 0.28, fr * 0.06);
      case GotchiMood.sleeping:
        _eyeClosed(canvas, lex, ey, fr * 0.17);
        _eyeClosed(canvas, rex, ey, fr * 0.17);
        _mouthSmile(canvas, cx, my - fr * 0.05, fr * 0.25);
        _drawZzz(canvas, cx + fr * 0.5, cy - fr * 0.8);
    }
  }

  // ── Ojos ─────────────────────────────────────────────────────────────────

  void _eyeOpen(Canvas canvas, double x, double y, double r) {
    canvas.drawCircle(Offset(x, y), r, Paint()..color = Colors.white);
    canvas.drawCircle(Offset(x + r * 0.15, y + r * 0.15), r * 0.5,
        Paint()..color = Colors.black);
  }

  void _eyeWide(Canvas canvas, double x, double y, double r) {
    canvas.drawCircle(Offset(x, y), r, Paint()..color = Colors.white);
    canvas.drawCircle(Offset(x, y), r,
        Paint()
          ..color = Colors.black
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5);
    canvas.drawCircle(Offset(x + r * 0.2, y + r * 0.2), r * 0.5,
        Paint()..color = Colors.black);
  }

  void _eyeHalf(Canvas canvas, double x, double y, double r) {
    canvas.save();
    canvas.clipRect(Rect.fromLTRB(x - r - 2, y, x + r + 2, y + r + 2));
    canvas.drawOval(
        Rect.fromCircle(center: Offset(x, y), radius: r),
        Paint()..color = Colors.white);
    canvas.restore();
    canvas.drawCircle(Offset(x + r * 0.1, y + r * 0.3), r * 0.42,
        Paint()..color = Colors.black);
  }

  void _eyeClosed(Canvas canvas, double x, double y, double r) {
    canvas.drawLine(Offset(x - r, y), Offset(x + r, y),
        Paint()
          ..color = Colors.black
          ..strokeWidth = 2.5
          ..style = PaintingStyle.stroke);
  }

  void _eyeHappy(Canvas canvas, double x, double y, double r) {
    final path = Path()
      ..moveTo(x - r, y + r * 0.4)
      ..quadraticBezierTo(x, y - r * 0.5, x + r, y + r * 0.4);
    canvas.drawPath(path,
        Paint()
          ..color = Colors.black
          ..style = PaintingStyle.stroke
          ..strokeWidth = 3
          ..strokeCap = StrokeCap.round);
  }

  void _eyeAngry(Canvas canvas, double x, double y, double r, {required bool left}) {
    _eyeOpen(canvas, x, y, r);
    final p = Paint()
      ..color = Colors.black
      ..strokeWidth = 3;
    if (left) {
      canvas.drawLine(Offset(x - r, y - r * 0.9), Offset(x + r * 0.3, y - r * 1.5), p);
    } else {
      canvas.drawLine(Offset(x - r * 0.3, y - r * 1.5), Offset(x + r, y - r * 0.9), p);
    }
  }

  void _eyeX(Canvas canvas, double x, double y, double r) {
    final p = Paint()
      ..color = Colors.black
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(Offset(x - r, y - r), Offset(x + r, y + r), p);
    canvas.drawLine(Offset(x + r, y - r), Offset(x - r, y + r), p);
  }

  void _eyeStar(Canvas canvas, double x, double y, double r) {
    final p = Paint()
      ..color = Colors.amber
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round;
    for (int i = 0; i < 4; i++) {
      final angle = i * math.pi / 4;
      canvas.drawLine(
          Offset(x + r * 0.25 * math.cos(angle), y + r * 0.25 * math.sin(angle)),
          Offset(x + r * math.cos(angle), y + r * math.sin(angle)),
          p);
    }
    canvas.drawCircle(Offset(x, y), r * 0.25, Paint()..color = Colors.amber);
  }

  // ── Bocas ─────────────────────────────────────────────────────────────────

  void _mouthSmile(Canvas canvas, double cx, double cy, double r) {
    final path = Path()
      ..moveTo(cx - r, cy)
      ..quadraticBezierTo(cx, cy + r * 0.7, cx + r, cy);
    canvas.drawPath(path,
        Paint()
          ..color = Colors.black
          ..style = PaintingStyle.stroke
          ..strokeWidth = 3
          ..strokeCap = StrokeCap.round);
  }

  void _mouthFrown(Canvas canvas, double cx, double cy, double r) {
    final path = Path()
      ..moveTo(cx - r, cy + r * 0.5)
      ..quadraticBezierTo(cx, cy - r * 0.3, cx + r, cy + r * 0.5);
    canvas.drawPath(path,
        Paint()
          ..color = Colors.black
          ..style = PaintingStyle.stroke
          ..strokeWidth = 3
          ..strokeCap = StrokeCap.round);
  }

  void _mouthOpen(Canvas canvas, double cx, double cy, double rx, double ry) {
    canvas.drawOval(
        Rect.fromCenter(center: Offset(cx, cy), width: rx * 2, height: ry * 2),
        Paint()..color = Colors.black);
  }

  void _mouthGrin(Canvas canvas, double cx, double cy, double w, double h) {
    canvas.drawRRect(
        RRect.fromRectAndRadius(
            Rect.fromCenter(center: Offset(cx, cy), width: w, height: h),
            const Radius.circular(6)),
        Paint()..color = Colors.black);
    canvas.drawRRect(
        RRect.fromRectAndRadius(
            Rect.fromCenter(center: Offset(cx, cy - h * 0.1), width: w - 4, height: h * 0.7),
            const Radius.circular(4)),
        Paint()..color = Colors.white);
    final lp = Paint()
      ..color = Colors.black
      ..strokeWidth = 1.5;
    canvas.drawLine(Offset(cx - w * 0.15, cy - h * 0.45), Offset(cx - w * 0.15, cy + h * 0.25), lp);
    canvas.drawLine(Offset(cx + w * 0.15, cy - h * 0.45), Offset(cx + w * 0.15, cy + h * 0.25), lp);
  }

  void _mouthTongue(Canvas canvas, double cx, double cy, double r) {
    canvas.drawOval(
        Rect.fromCenter(center: Offset(cx, cy), width: r * 2, height: r * 1.4),
        Paint()..color = Colors.black);
    canvas.drawOval(
        Rect.fromCenter(center: Offset(cx, cy + r * 0.4), width: r * 1.4, height: r),
        Paint()..color = Colors.pinkAccent);
  }

  void _mouthWavy(Canvas canvas, double cx, double cy, double r) {
    final path = Path()..moveTo(cx - r, cy);
    const segments = 6;
    final segW = r * 2 / segments;
    for (int i = 0; i < segments; i++) {
      final x0 = cx - r + i * segW;
      final x1 = x0 + segW;
      final yOff = (i % 2 == 0) ? -8.0 : 8.0;
      path.quadraticBezierTo(x0 + segW / 2, cy + yOff, x1, cy);
    }
    canvas.drawPath(path,
        Paint()
          ..color = Colors.black
          ..style = PaintingStyle.stroke
          ..strokeWidth = 3
          ..strokeCap = StrokeCap.round);
  }

  // ── Extras ────────────────────────────────────────────────────────────────

  void _drawTear(Canvas canvas, double x, double y, double r) {
    canvas.drawCircle(Offset(x, y), r, Paint()..color = Colors.cyan);
    canvas.drawPath(
        Path()
          ..moveTo(x - r, y)
          ..lineTo(x + r, y)
          ..lineTo(x, y + r * 2.5)
          ..close(),
        Paint()..color = Colors.cyan);
  }

  void _drawSweat(Canvas canvas, double x, double y, double r) {
    canvas.drawCircle(Offset(x, y), r, Paint()..color = Colors.lightBlue);
    canvas.drawPath(
        Path()
          ..moveTo(x - r * 0.7, y)
          ..lineTo(x + r * 0.7, y)
          ..lineTo(x, y + r * 2)
          ..close(),
        Paint()..color = Colors.lightBlue);
  }

  void _drawZzz(Canvas canvas, double x, double y) {
    final sizes  = [14.0, 18.0, 22.0];
    final xOff   = [0.0, 10.0, 22.0];
    final yOff   = [0.0, -14.0, -28.0];
    final alphas = [0.5, 0.7, 1.0];
    for (int i = 0; i < 3; i++) {
      final tp = TextPainter(
          text: TextSpan(
              text: i == 0 ? 'z' : 'Z',
              style: TextStyle(
                  color: Colors.white.withOpacity(alphas[i]),
                  fontSize: sizes[i],
                  fontWeight: FontWeight.bold)),
          textDirection: TextDirection.ltr)
        ..layout();
      tp.paint(canvas, Offset(x + xOff[i], y + yOff[i]));
    }
  }

  // ── Colores por mood ──────────────────────────────────────────────────────

  Color _bgColor() => switch (mood) {
        GotchiMood.happy    => const Color(0xFF88DDAA),
        GotchiMood.tired    => const Color(0xFF334466),
        GotchiMood.hungry   => const Color(0xFFCC6600),
        GotchiMood.thirsty  => const Color(0xFF0077AA),
        GotchiMood.dizzy    => const Color(0xFF330055),
        GotchiMood.excited  => const Color(0xFFFFDD00),
        GotchiMood.scared   => const Color(0xFF111122),
        GotchiMood.laughing => const Color(0xFF99FF44),
        GotchiMood.angry    => const Color(0xFFCC0000),
        GotchiMood.sad      => const Color(0xFF1144AA),
        GotchiMood.sleeping => const Color(0xFF050520),
      };

  Color _faceColor() => switch (mood) {
        GotchiMood.tired    => const Color(0xFFDDCCAA),
        GotchiMood.dizzy    => const Color(0xFFEEBBFF),
        GotchiMood.scared   => const Color(0xFFEEEECC),
        GotchiMood.angry    => const Color(0xFFFFBB88),
        GotchiMood.sad      => const Color(0xFFCCDDFF),
        GotchiMood.sleeping => const Color(0xFFBBCCEE),
        _                   => const Color(0xFFFFE000),
      };

  @override
  bool shouldRepaint(_FacePainter old) => old.mood != mood;
}
