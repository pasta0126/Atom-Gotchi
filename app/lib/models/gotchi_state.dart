enum GotchiMood {
  happy,
  tired,
  hungry,
  thirsty,
  dizzy,
  excited,
  scared,
  laughing,
  angry,
  sad,
  sleeping,
  startled,
  annoyed,
}

extension GotchiMoodExtension on GotchiMood {
  String get emoji {
    switch (this) {
      case GotchiMood.happy:    return '😊';
      case GotchiMood.tired:    return '😴';
      case GotchiMood.hungry:   return '🍔';
      case GotchiMood.thirsty:  return '💧';
      case GotchiMood.dizzy:    return '😵';
      case GotchiMood.excited:  return '🤩';
      case GotchiMood.scared:   return '😱';
      case GotchiMood.laughing: return '😂';
      case GotchiMood.angry:    return '😠';
      case GotchiMood.sad:      return '😢';
      case GotchiMood.sleeping: return '💤';
      case GotchiMood.startled: return '😨';
      case GotchiMood.annoyed:  return '😤';
    }
  }

  String get label {
    switch (this) {
      case GotchiMood.happy:    return 'Feliz';
      case GotchiMood.tired:    return 'Cansado';
      case GotchiMood.hungry:   return 'Hambriento';
      case GotchiMood.thirsty:  return 'Con sed';
      case GotchiMood.dizzy:    return 'Mareado';
      case GotchiMood.excited:  return '¡Emocionado!';
      case GotchiMood.scared:   return 'Asustado';
      case GotchiMood.laughing: return 'Riéndose';
      case GotchiMood.angry:    return '¡Enfadado!';
      case GotchiMood.sad:      return 'Triste';
      case GotchiMood.sleeping: return 'Durmiendo';
      case GotchiMood.startled: return '¡Sobresaltado!';
      case GotchiMood.annoyed:  return 'Contrariado';
    }
  }
}

class GotchiState {
  final GotchiMood mood;
  final int hunger;   // 0-100
  final int thirst;   // 0-100
  final int energy;   // 0-100
  final int steps;
  final bool phoneBatteryWarn;

  const GotchiState({
    this.mood = GotchiMood.happy,
    this.hunger = 80,
    this.thirst = 80,
    this.energy = 90,
    this.steps = 0,
    this.phoneBatteryWarn = false,
  });

  factory GotchiState.fromBytes(List<int> bytes) {
    if (bytes.length < 6) return const GotchiState();
    final moodIndex = bytes[0].clamp(0, GotchiMood.values.length - 1);
    return GotchiState(
      mood:             GotchiMood.values[moodIndex],
      hunger:           bytes[1],
      thirst:           bytes[2],
      energy:           bytes[3],
      steps:            (bytes[4] << 8) | bytes[5],
      phoneBatteryWarn: bytes.length > 6 && (bytes[6] & 0x01) != 0,
    );
  }
}
