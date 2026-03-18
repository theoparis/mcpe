cmake_minimum_required(VERSION 4.0)

project(mcpe C CXX ASM OBJCXX)

include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_CLANG_SCAN_FOR_MODULES OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(BUILD_CLIENT "Build the client" ON)
option(MCPE_ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(MCPE_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

function(mcpe_apply_sanitizers target)
    if (NOT MCPE_ENABLE_ASAN AND NOT MCPE_ENABLE_UBSAN)
        return()
    endif()

    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "ASAN/UBSAN are only supported with Clang or GCC in this build")
    endif()

    set(MCPE_SANITIZER_FLAGS "-fno-omit-frame-pointer")
    if (MCPE_ENABLE_ASAN)
        list(APPEND MCPE_SANITIZER_FLAGS "-fsanitize=address")
    endif()
    if (MCPE_ENABLE_UBSAN)
        list(APPEND MCPE_SANITIZER_FLAGS "-fsanitize=undefined")
    endif()

    target_compile_options(
        ${target}
        PRIVATE
        ${MCPE_SANITIZER_FLAGS}
    )
    target_link_options(
        ${target}
        PRIVATE
        ${MCPE_SANITIZER_FLAGS}
    )
endfunction()

find_package(PkgConfig REQUIRED)

if(BUILD_CLIENT)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    pkg_check_modules(OpenAL REQUIRED IMPORTED_TARGET openal)
    pkg_check_modules(libpng REQUIRED IMPORTED_TARGET libpng)
    find_package(Vulkan REQUIRED)

    set(MCPE_VULKAN_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${MCPE_VULKAN_SHADER_DIR}")
    set(MCPE_VULKAN_SHADER_SOURCES
        "handheld/src/platform/sdl3/shaders/textured_quad.vert"
        "handheld/src/platform/sdl3/shaders/textured_quad.frag"
        "handheld/src/platform/sdl3/shaders/terrain_mesh.vert"
        "handheld/src/platform/sdl3/shaders/terrain_mesh.frag"
        "handheld/src/platform/sdl3/shaders/terrain_mesh_alpha_test.frag"
    )
    set(MCPE_VULKAN_SHADER_OUTPUTS)
    foreach(shader IN LISTS MCPE_VULKAN_SHADER_SOURCES)
        get_filename_component(shader_name "${shader}" NAME)
        set(shader_output "${MCPE_VULKAN_SHADER_DIR}/${shader_name}.spv")
        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -o "${shader_output}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
            VERBATIM
        )
        list(APPEND MCPE_VULKAN_SHADER_OUTPUTS "${shader_output}")
    endforeach()
    add_custom_target(mcpe_vulkan_shaders DEPENDS ${MCPE_VULKAN_SHADER_OUTPUTS})
endif()

set(SOURCES
    "handheld/src/client/IConfigListener.cpp"
    "handheld/src/MinecraftApp.cpp"
    "handheld/src/Performance.cpp"
    "handheld/src/SharedConstants.cpp"
    "handheld/src/locale/I18n.cpp"
    "handheld/src/client/player/LocalPlayer.cpp"
    "handheld/src/client/player/RemotePlayer.cpp"
    "handheld/src/client/gamemode/CreativeMode.cpp"
    "handheld/src/client/gamemode/CreatorMode.cpp"
    "handheld/src/client/gamemode/GameMode.cpp"
    "handheld/src/client/gamemode/SurvivalMode.cpp"
    "handheld/src/client/Minecraft.cpp"
    "handheld/src/client/OptionStrings.cpp"
    "handheld/src/client/Options.cpp"
    "handheld/src/client/OptionsFile.cpp"
    "handheld/src/nbt/Tag.cpp"
    "handheld/src/network/NATPunchHandler.cpp"
    "handheld/src/network/NetEventCallback.cpp"
    "handheld/src/network/PHPDirectoryServer2.cpp"
    "handheld/src/network/Packet.cpp"
    "handheld/src/network/RakNetInstance.cpp"
    "handheld/src/network/ServerSideNetworkHandler.cpp"
    "handheld/src/network/command/CommandServer.cpp"
    "handheld/src/platform/time.cpp"
    "handheld/src/raknet/BitStream.cpp"
    "handheld/src/raknet/CCRakNetSlidingWindow.cpp"
    "handheld/src/raknet/CCRakNetUDT.cpp"
    "handheld/src/raknet/CheckSum.cpp"
    "handheld/src/raknet/CloudClient.cpp"
    "handheld/src/raknet/CloudCommon.cpp"
    "handheld/src/raknet/CloudServer.cpp"
    "handheld/src/raknet/CommandParserInterface.cpp"
    "handheld/src/raknet/ConnectionGraph2.cpp"
    "handheld/src/raknet/ConsoleServer.cpp"
    "handheld/src/raknet/DS_BytePool.cpp"
    "handheld/src/raknet/DS_ByteQueue.cpp"
    "handheld/src/raknet/DS_HuffmanEncodingTree.cpp"
    "handheld/src/raknet/DS_Table.cpp"
    "handheld/src/raknet/DataCompressor.cpp"
    "handheld/src/raknet/DirectoryDeltaTransfer.cpp"
    "handheld/src/raknet/DynDNS.cpp"
    "handheld/src/raknet/EmailSender.cpp"
    "handheld/src/raknet/EncodeClassName.cpp"
    "handheld/src/raknet/EpochTimeToString.cpp"
    "handheld/src/raknet/FileList.cpp"
    "handheld/src/raknet/FileListTransfer.cpp"
    "handheld/src/raknet/FileOperations.cpp"
    "handheld/src/raknet/FormatString.cpp"
    "handheld/src/raknet/FullyConnectedMesh2.cpp"
    "handheld/src/raknet/GetTime.cpp"
    "handheld/src/raknet/Getche.cpp"
    "handheld/src/raknet/Gets.cpp"
    "handheld/src/raknet/GridSectorizer.cpp"
    "handheld/src/raknet/HTTPConnection.cpp"
    "handheld/src/raknet/IncrementalReadInterface.cpp"
    "handheld/src/raknet/Itoa.cpp"
    "handheld/src/raknet/LinuxStrings.cpp"
    "handheld/src/raknet/LocklessTypes.cpp"
    "handheld/src/raknet/LogCommandParser.cpp"
    "handheld/src/raknet/MessageFilter.cpp"
    "handheld/src/raknet/NatPunchthroughClient.cpp"
    "handheld/src/raknet/NatPunchthroughServer.cpp"
    "handheld/src/raknet/NatTypeDetectionClient.cpp"
    "handheld/src/raknet/NatTypeDetectionCommon.cpp"
    "handheld/src/raknet/NatTypeDetectionServer.cpp"
    "handheld/src/raknet/NetworkIDManager.cpp"
    "handheld/src/raknet/NetworkIDObject.cpp"
    "handheld/src/raknet/PacketConsoleLogger.cpp"
    "handheld/src/raknet/PacketFileLogger.cpp"
    "handheld/src/raknet/PacketLogger.cpp"
    "handheld/src/raknet/PacketOutputWindowLogger.cpp"
    "handheld/src/raknet/PacketizedTCP.cpp"
    "handheld/src/raknet/PluginInterface2.cpp"
    "handheld/src/raknet/RPC4Plugin.cpp"
    "handheld/src/raknet/Rackspace.cpp"
    "handheld/src/raknet/RakMemoryOverride.cpp"
    "handheld/src/raknet/RakNetCommandParser.cpp"
    "handheld/src/raknet/RakNetSocket.cpp"
    "handheld/src/raknet/RakNetStatistics.cpp"
    "handheld/src/raknet/RakNetTransport2.cpp"
    "handheld/src/raknet/RakNetTypes.cpp"
    "handheld/src/raknet/RakPeer.cpp"
    "handheld/src/raknet/RakSleep.cpp"
    "handheld/src/raknet/RakString.cpp"
    "handheld/src/raknet/RakThread.cpp"
    "handheld/src/raknet/RakWString.cpp"
    "handheld/src/raknet/Rand.cpp"
    "handheld/src/raknet/ReadyEvent.cpp"
    "handheld/src/raknet/ReliabilityLayer.cpp"
    "handheld/src/raknet/ReplicaManager3.cpp"
    "handheld/src/raknet/Router2.cpp"
    "handheld/src/raknet/SHA1.cpp"
    "handheld/src/raknet/SecureHandshake.cpp"
    "handheld/src/raknet/SendToThread.cpp"
    "handheld/src/raknet/SignaledEvent.cpp"
    "handheld/src/raknet/SimpleMutex.cpp"
    "handheld/src/raknet/SocketLayer.cpp"
    "handheld/src/raknet/StringCompressor.cpp"
    "handheld/src/raknet/StringTable.cpp"
    "handheld/src/raknet/SuperFastHash.cpp"
    "handheld/src/raknet/TCPInterface.cpp"
    "handheld/src/raknet/TableSerializer.cpp"
    "handheld/src/raknet/TeamBalancer.cpp"
    "handheld/src/raknet/TelnetTransport.cpp"
    "handheld/src/raknet/ThreadsafePacketLogger.cpp"
    "handheld/src/raknet/TwoWayAuthentication.cpp"
    "handheld/src/raknet/UDPForwarder.cpp"
    "handheld/src/raknet/UDPProxyClient.cpp"
    "handheld/src/raknet/UDPProxyCoordinator.cpp"
    "handheld/src/raknet/UDPProxyServer.cpp"
    "handheld/src/raknet/VariableDeltaSerializer.cpp"
    "handheld/src/raknet/VariableListDeltaTracker.cpp"
    "handheld/src/raknet/VariadicSQLParser.cpp"
    "handheld/src/raknet/VitaIncludes.cpp"
    "handheld/src/raknet/WSAStartupSingleton.cpp"
    "handheld/src/raknet/_FindFirst.cpp"
    "handheld/src/raknet/gettimeofday.cpp"
    "handheld/src/raknet/rdlmalloc.cpp"
    "handheld/src/server/ArgumentsSettings.cpp"
    "handheld/src/server/CreatorLevel.cpp"
    "handheld/src/server/ServerLevel.cpp"
    "handheld/src/server/ServerPlayer.cpp"
    "handheld/src/util/DataIO.cpp"
    "handheld/src/util/Mth.cpp"
    "handheld/src/util/PerfTimer.cpp"
    "handheld/src/util/StringUtils.cpp"
    "handheld/src/world/Direction.cpp"
    "handheld/src/world/entity/AgableMob.cpp"
    "handheld/src/world/entity/Entity.cpp"
    "handheld/src/world/entity/EntityFactory.cpp"
    "handheld/src/world/entity/FlyingMob.cpp"
    "handheld/src/world/entity/HangingEntity.cpp"
    "handheld/src/world/entity/Mob.cpp"
    "handheld/src/world/entity/MobCategory.cpp"
    "handheld/src/world/entity/Motive.cpp"
    "handheld/src/world/entity/Painting.cpp"
    "handheld/src/world/entity/PathfinderMob.cpp"
    "handheld/src/world/entity/SynchedEntityData.cpp"
    "handheld/src/world/entity/ai/control/MoveControl.cpp"
    "handheld/src/world/entity/animal/Animal.cpp"
    "handheld/src/world/entity/animal/Chicken.cpp"
    "handheld/src/world/entity/animal/Cow.cpp"
    "handheld/src/world/entity/animal/Pig.cpp"
    "handheld/src/world/entity/animal/Sheep.cpp"
    "handheld/src/world/entity/animal/WaterAnimal.cpp"
    "handheld/src/world/entity/item/FallingTile.cpp"
    "handheld/src/world/entity/item/ItemEntity.cpp"
    "handheld/src/world/entity/item/PrimedTnt.cpp"
    "handheld/src/world/entity/item/TripodCamera.cpp"
    "handheld/src/world/entity/monster/Creeper.cpp"
    "handheld/src/world/entity/monster/Monster.cpp"
    "handheld/src/world/entity/monster/PigZombie.cpp"
    "handheld/src/world/entity/monster/Skeleton.cpp"
    "handheld/src/world/entity/monster/Spider.cpp"
    "handheld/src/world/entity/monster/Zombie.cpp"
    "handheld/src/world/entity/player/Inventory.cpp"
    "handheld/src/world/entity/player/Player.cpp"
    "handheld/src/world/entity/projectile/Arrow.cpp"
    "handheld/src/world/entity/projectile/Throwable.cpp"
    "handheld/src/world/food/SimpleFoodData.cpp"
    "handheld/src/world/inventory/BaseContainerMenu.cpp"
    "handheld/src/world/inventory/ContainerMenu.cpp"
    "handheld/src/world/inventory/FillingContainer.cpp"
    "handheld/src/world/inventory/FurnaceMenu.cpp"
    "handheld/src/world/item/ArmorItem.cpp"
    "handheld/src/world/item/BedItem.cpp"
    "handheld/src/world/item/DyePowderItem.cpp"
    "handheld/src/world/item/HangingEntityItem.cpp"
    "handheld/src/world/item/HatchetItem.cpp"
    "handheld/src/world/item/HoeItem.cpp"
    "handheld/src/world/item/Item.cpp"
    "handheld/src/world/item/ItemInstance.cpp"
    "handheld/src/world/item/PickaxeItem.cpp"
    "handheld/src/world/item/ShovelItem.cpp"
    "handheld/src/world/item/crafting/ArmorRecipes.cpp"
    "handheld/src/world/item/crafting/FurnaceRecipes.cpp"
    "handheld/src/world/item/crafting/OreRecipes.cpp"
    "handheld/src/world/item/crafting/Recipe.cpp"
    "handheld/src/world/item/crafting/Recipes.cpp"
    "handheld/src/world/item/crafting/StructureRecipes.cpp"
    "handheld/src/world/item/crafting/ToolRecipes.cpp"
    "handheld/src/world/item/crafting/WeaponRecipes.cpp"
    "handheld/src/world/level/Explosion.cpp"
    "handheld/src/world/level/Level.cpp"
    "handheld/src/world/level/LightLayer.cpp"
    "handheld/src/world/level/LightUpdate.cpp"
    "handheld/src/world/level/MobSpawner.cpp"
    "handheld/src/world/level/Region.cpp"
    "handheld/src/world/level/TickNextTickData.cpp"
    "handheld/src/world/level/biome/Biome.cpp"
    "handheld/src/world/level/biome/BiomeSource.cpp"
    "handheld/src/world/level/chunk/LevelChunk.cpp"
    "handheld/src/world/level/dimension/Dimension.cpp"
    "handheld/src/world/level/levelgen/CanyonFeature.cpp"
    "handheld/src/world/level/levelgen/DungeonFeature.cpp"
    "handheld/src/world/level/levelgen/LargeCaveFeature.cpp"
    "handheld/src/world/level/levelgen/LargeFeature.cpp"
    "handheld/src/world/level/levelgen/RandomLevelSource.cpp"
    "handheld/src/world/level/levelgen/feature/Feature.cpp"
    "handheld/src/world/level/levelgen/synth/ImprovedNoise.cpp"
    "handheld/src/world/level/levelgen/synth/PerlinNoise.cpp"
    "handheld/src/world/level/levelgen/synth/Synth.cpp"
    "handheld/src/world/level/material/Material.cpp"
    "handheld/src/world/level/pathfinder/Path.cpp"
    "handheld/src/world/level/storage/ExternalFileLevelStorage.cpp"
    "handheld/src/world/level/storage/ExternalFileLevelStorageSource.cpp"
    "handheld/src/world/level/storage/FolderMethods.cpp"
    "handheld/src/world/level/storage/LevelData.cpp"
    "handheld/src/world/level/storage/LevelStorageSource.cpp"
    "handheld/src/world/level/storage/RegionFile.cpp"
    "handheld/src/world/level/tile/BedTile.cpp"
    "handheld/src/world/level/tile/ChestTile.cpp"
    "handheld/src/world/level/tile/CropTile.cpp"
    "handheld/src/world/level/tile/DoorTile.cpp"
    "handheld/src/world/level/tile/EntityTile.cpp"
    "handheld/src/world/level/tile/FurnaceTile.cpp"
    "handheld/src/world/level/tile/GrassTile.cpp"
    "handheld/src/world/level/tile/HeavyTile.cpp"
    "handheld/src/world/level/tile/LightGemTile.cpp"
    "handheld/src/world/level/tile/MelonTile.cpp"
    "handheld/src/world/level/tile/Mushroom.cpp"
    "handheld/src/world/level/tile/NetherReactor.cpp"
    "handheld/src/world/level/tile/NetherReactorPattern.cpp"
    "handheld/src/world/level/tile/SandTile.cpp"
    "handheld/src/world/level/tile/StairTile.cpp"
    "handheld/src/world/level/tile/StemTile.cpp"
    "handheld/src/world/level/tile/StoneSlabTile.cpp"
    "handheld/src/world/level/tile/TallGrass.cpp"
    "handheld/src/world/level/tile/Tile.cpp"
    "handheld/src/world/level/tile/TrapDoorTile.cpp"
    "handheld/src/world/level/tile/entity/ChestTileEntity.cpp"
    "handheld/src/world/level/tile/entity/FurnaceTileEntity.cpp"
    "handheld/src/world/level/tile/entity/NetherReactorTileEntity.cpp"
    "handheld/src/world/level/tile/entity/SignTileEntity.cpp"
    "handheld/src/world/level/tile/entity/TileEntity.cpp"
    "handheld/src/world/phys/HitResult.cpp"
)

add_executable(mcpe_dedicated
    handheld/src/main_dedicated.cpp
    ${SOURCES}
)
target_compile_definitions(
    mcpe_dedicated
    PRIVATE
    STANDALONE_SERVER
)
target_include_directories(
    mcpe_dedicated
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/handheld/src>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(
    mcpe_dedicated
    PRIVATE
    cxx_std_23
)
mcpe_apply_sanitizers(mcpe_dedicated)

add_executable(mcpe_sdl3
    ${SOURCES}
    handheld/src/main.cpp
    handheld/src/main_sdl3.cpp
    "handheld/src/client/gui/Font.cpp"
    "handheld/src/client/gui/Gui.cpp"
    "handheld/src/client/gui/GuiComponent.cpp"
    "handheld/src/client/gui/Screen.cpp"
    "handheld/src/client/gui/components/Button.cpp"
    "handheld/src/client/gui/components/GButton.cpp"
    "handheld/src/client/gui/components/GuiElement.cpp"
    "handheld/src/client/gui/components/GuiElementContainer.cpp"
    "handheld/src/client/gui/components/ImageButton.cpp"
    "handheld/src/client/gui/components/InventoryPane.cpp"
    "handheld/src/client/gui/components/ItemPane.cpp"
    "handheld/src/client/gui/components/LargeImageButton.cpp"
    "handheld/src/client/gui/components/NinePatch.cpp"
    "handheld/src/client/gui/components/OptionsGroup.cpp"
    "handheld/src/client/gui/components/OptionsItem.cpp"
    "handheld/src/client/gui/components/OptionsPane.cpp"
    "handheld/src/client/gui/components/RolledSelectionListH.cpp"
    "handheld/src/client/gui/components/RolledSelectionListV.cpp"
    "handheld/src/client/gui/components/ScrolledSelectionList.cpp"
    "handheld/src/client/gui/components/ScrollingPane.cpp"
    "handheld/src/client/gui/components/Slider.cpp"
    "handheld/src/client/gui/components/SmallButton.cpp"
    "handheld/src/client/gui/components/TextBox.cpp"
    "handheld/src/client/gui/screens/ArmorScreen.cpp"
    "handheld/src/client/gui/screens/ChatScreen.cpp"
    "handheld/src/client/gui/screens/ChestScreen.cpp"
    "handheld/src/client/gui/screens/ChooseLevelScreen.cpp"
    "handheld/src/client/gui/screens/ConfirmScreen.cpp"
    "handheld/src/client/gui/screens/DeathScreen.cpp"
    "handheld/src/client/gui/screens/FurnaceScreen.cpp"
    "handheld/src/client/gui/screens/InBedScreen.cpp"
    "handheld/src/client/gui/screens/IngameBlockSelectionScreen.cpp"
    "handheld/src/client/gui/screens/JoinGameScreen.cpp"
    "handheld/src/client/gui/screens/OptionsScreen.cpp"
    "handheld/src/client/gui/screens/PauseScreen.cpp"
    "handheld/src/client/gui/screens/ProgressScreen.cpp"
    "handheld/src/client/gui/screens/RenameMPLevelScreen.cpp"
    "handheld/src/client/gui/screens/ScreenChooser.cpp"
    "handheld/src/client/gui/screens/SelectWorldScreen.cpp"
    "handheld/src/client/gui/screens/SimpleChooseLevelScreen.cpp"
    "handheld/src/client/gui/screens/StartMenuScreen.cpp"
    "handheld/src/client/gui/screens/TextEditScreen.cpp"
    "handheld/src/client/gui/screens/UploadPhotoScreen.cpp"
    "handheld/src/client/gui/screens/crafting/CraftingFilters.cpp"
    "handheld/src/client/gui/screens/crafting/PaneCraftingScreen.cpp"
    "handheld/src/client/gui/screens/crafting/StonecutterScreen.cpp"
    "handheld/src/client/gui/screens/crafting/WorkbenchScreen.cpp"
    "handheld/src/client/gui/screens/touch/TouchIngameBlockSelectionScreen.cpp"
    "handheld/src/client/gui/screens/touch/TouchJoinGameScreen.cpp"
    "handheld/src/client/gui/screens/touch/TouchSelectWorldScreen.cpp"
    "handheld/src/client/gui/screens/touch/TouchStartMenuScreen.cpp"
    "handheld/src/client/model/ChickenModel.cpp"
    "handheld/src/client/model/CowModel.cpp"
    "handheld/src/client/model/HumanoidModel.cpp"
    "handheld/src/client/model/PigModel.cpp"
    "handheld/src/client/model/QuadrupedModel.cpp"
    "handheld/src/client/model/SheepFurModel.cpp"
    "handheld/src/client/model/SheepModel.cpp"
    "handheld/src/client/model/geom/Cube.cpp"
    "handheld/src/client/model/geom/ModelPart.cpp"
    "handheld/src/client/model/geom/Polygon.cpp"
    "handheld/src/client/particle/Particle.cpp"
    "handheld/src/client/particle/ParticleEngine.cpp"
    "handheld/src/client/player/input/KeyboardInput.cpp"
    "handheld/src/client/player/input/touchscreen/TouchscreenInput.cpp"
    "handheld/src/client/renderer/Chunk.cpp"
    "handheld/src/client/renderer/gles.cpp"
    "handheld/src/client/renderer/EntityTileRenderer.cpp"
    "handheld/src/client/renderer/GameRenderer.cpp"
    "handheld/src/client/renderer/ItemInHandRenderer.cpp"
    "handheld/src/client/renderer/LevelRenderer.cpp"
    "handheld/src/client/renderer/RenderChunk.cpp"
    "handheld/src/client/renderer/RenderList.cpp"
    "handheld/src/client/renderer/Tesselator.cpp"
    "handheld/src/client/renderer/Textures.cpp"
    "handheld/src/client/renderer/TileRenderer.cpp"
    "handheld/src/client/renderer/culling/Frustum.cpp"
    "handheld/src/client/renderer/entity/ArrowRenderer.cpp"
    "handheld/src/client/renderer/entity/ChickenRenderer.cpp"
    "handheld/src/client/renderer/entity/EntityRenderDispatcher.cpp"
    "handheld/src/client/renderer/entity/EntityRenderer.cpp"
    "handheld/src/client/renderer/entity/FallingTileRenderer.cpp"
    "handheld/src/client/renderer/entity/HumanoidMobRenderer.cpp"
    "handheld/src/client/renderer/entity/ItemRenderer.cpp"
    "handheld/src/client/renderer/entity/ItemSpriteRenderer.cpp"
    "handheld/src/client/renderer/entity/MobRenderer.cpp"
    "handheld/src/client/renderer/entity/PaintingRenderer.cpp"
    "handheld/src/client/renderer/entity/PlayerRenderer.cpp"
    "handheld/src/client/renderer/entity/SheepRenderer.cpp"
    "handheld/src/client/renderer/entity/TntRenderer.cpp"
    "handheld/src/client/renderer/entity/TripodCameraRenderer.cpp"
    "handheld/src/client/renderer/ptexture/DynamicTexture.cpp"
    "handheld/src/client/renderer/tileentity/ChestRenderer.cpp"
    "handheld/src/client/renderer/tileentity/SignRenderer.cpp"
    "handheld/src/client/renderer/tileentity/TileEntityRenderDispatcher.cpp"
    "handheld/src/client/renderer/tileentity/TileEntityRenderer.cpp"
    "handheld/src/network/ClientSideNetworkHandler.cpp"
    "handheld/src/client/MouseHandler.cpp"
    "handheld/src/client/sound/Sound.cpp"
    "handheld/src/client/sound/SoundEngine.cpp"
    "handheld/src/util/PerfRenderer.cpp"
    "handheld/src/platform/audio/SoundSystemAL.cpp"
    "handheld/src/platform/input/Controller.cpp"
    "handheld/src/platform/input/Keyboard.cpp"
    "handheld/src/platform/input/Mouse.cpp"
    "handheld/src/platform/input/Multitouch.cpp"
    "handheld/src/platform/sdl3/Sdl3VulkanBackend.cpp"
)
target_link_libraries(
    mcpe_sdl3
    PRIVATE
    PkgConfig::SDL3
    PkgConfig::OpenAL
    PkgConfig::libpng
    Vulkan::Vulkan
)
target_compile_definitions(
    mcpe_sdl3
    PUBLIC
    SDL3
)
target_link_directories(
    mcpe_sdl3
    PUBLIC
    ${OpenAL_LIBRARY_DIRS}
    ${libpng_LIBRARY_DIRS}
)
target_compile_definitions(
    mcpe_sdl3
    PUBLIC
    SDL3
    OPENGL_ES
    MCPE_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/mcpe"
    MCPE_VULKAN_SHADER_DIR="${MCPE_VULKAN_SHADER_DIR}"
)
target_include_directories(
    mcpe_sdl3
    PUBLIC
    ${SDL3_INCLUDE_DIRS}
    ${libpng_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/handheld/src>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(
    mcpe_sdl3
    PRIVATE
    cxx_std_23
)
add_dependencies(mcpe_sdl3 mcpe_vulkan_shaders)
mcpe_apply_sanitizers(mcpe_sdl3)

if (APPLE)
    find_library(FOUNDATION_LIBRARY Foundation)
    target_link_libraries(
        mcpe_sdl3
        PRIVATE
        ${FOUNDATION_LIBRARY}
    )

    target_sources(
        mcpe_sdl3
        PRIVATE
        "handheld/src/world/level/storage/MoveFolder.mm"
    )
endif()

install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mcpe)

install(TARGETS mcpe_sdl3 mcpe_dedicated
    EXPORT mcpeTargets
    INCLUDES DESTINATION include
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
