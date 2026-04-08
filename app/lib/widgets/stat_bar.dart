import 'package:flutter/material.dart';

class StatBar extends StatelessWidget {
  final String icon;
  final int value; // 0-100
  final Color color;

  const StatBar({
    super.key,
    required this.icon,
    required this.value,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    final pct = (value / 100.0).clamp(0.0, 1.0);
    final isLow = value < 25;

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Text(icon, style: const TextStyle(fontSize: 18)),
          const SizedBox(width: 8),
          Expanded(
            child: ClipRRect(
              borderRadius: BorderRadius.circular(6),
              child: LinearProgressIndicator(
                value: pct,
                minHeight: 12,
                backgroundColor: Colors.white12,
                valueColor: AlwaysStoppedAnimation<Color>(
                  isLow ? Colors.redAccent : color,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
