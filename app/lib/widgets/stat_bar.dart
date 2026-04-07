import 'package:flutter/material.dart';

class StatBar extends StatelessWidget {
  final String label;
  final String icon;
  final int value; // 0-100
  final Color color;

  const StatBar({
    super.key,
    required this.label,
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
          const SizedBox(width: 6),
          SizedBox(
            width: 52,
            child: Text(
              label,
              style: TextStyle(
                color: isLow ? Colors.redAccent : Colors.white70,
                fontSize: 12,
                fontWeight: isLow ? FontWeight.bold : FontWeight.normal,
              ),
            ),
          ),
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
          const SizedBox(width: 8),
          SizedBox(
            width: 32,
            child: Text(
              '$value',
              textAlign: TextAlign.right,
              style: TextStyle(
                color: isLow ? Colors.redAccent : Colors.white60,
                fontSize: 12,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
