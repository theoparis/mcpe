#ifndef NET_MINECRAFT_WORLD_ENTITY__Painting_H__
#define NET_MINECRAFT_WORLD_ENTITY__Painting_H__
#include "HangingEntity.h"
#include "Motive.h"
class Painting : public HangingEntity {
  typedef HangingEntity super;

public:
  Painting(Level *level);
  Painting(Level *level, int xTile, int yTile, int zTile, int dir);
  Painting(Level *level, int x, int y, int z, int dir,
      const std::string &motiveName);

  void setRandomMotive(int dir);

  void addAdditonalSaveData(CompoundTag *tag);
  void readAdditionalSaveData(CompoundTag *tag);

  int getWidth();
  int getHeight();

  void dropItem();
  int getEntityTypeId() const;
  bool isPickable();

public:
  const Motive *motive;
};

#endif /* NET_MINECRAFT_WORLD_ENTITY__Painting_H__ */