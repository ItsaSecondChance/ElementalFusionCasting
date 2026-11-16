#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using ProcessButtonFn = void(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);

    constexpr RE::FormID kFlamesFormID = 0x00012FCD;
    constexpr RE::FormID kFrostbiteFormID = 0x0002B96B;
    constexpr RE::FormID kSparksFormID = 0x0002DD2A;
    constexpr float kDualPressWindow = 0.18F;
    constexpr float kSyntheticReleaseHeldSeconds = 0.10F;

    REL::Relocation<ProcessButtonFn> g_originalProcessButton;
    bool g_hookInstalled = false;

    struct ButtonSnapshot
    {
        std::size_t hand = 0;
        RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
        RE::BSFixedString userEvent;
        std::uint32_t idCode = 0;
        RE::PlayerControlsData* data = nullptr;
    };

    enum class InputMode
    {
        kIdle,
        kPendingFirstHand,
        kSingleHand,
        kDual
    };

    struct InputState
    {
        InputMode mode = InputMode::kIdle;
        ButtonSnapshot pending;
        std::size_t singleHand = 0;
        std::array<ButtonSnapshot, 2> dualButtons{};
        std::array<bool, 2> dualPhysicalDown{ false, false };
        std::array<RE::MagicItem*, 2> originalSpells{ nullptr, nullptr };
        RE::SpellItem* fusionSpell = nullptr;
    };

    InputState g_input;
    std::unordered_map<std::uint64_t, RE::SpellItem*> g_fusionSpells;

    void LogInfo(std::string_view message)
    {
        spdlog::info("[ElementalFusionCasting] {}", message);
    }

    void InitializeLogging()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("ElementalFusionCasting: unable to locate SKSE log directory.");
        }

        *path /= "ElementalFusionCasting.log";
        auto logger = std::make_shared<spdlog::logger>(
            "ElementalFusionCasting",
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }

    std::uint64_t MakePairKey(RE::FormID a_left, RE::FormID a_right)
    {
        const auto low = std::min(a_left, a_right);
        const auto high = std::max(a_left, a_right);
        return (static_cast<std::uint64_t>(low) << 32) | high;
    }

    RE::SpellItem* LookupVanillaSpell(RE::FormID a_formID)
    {
        return RE::TESForm::LookupByID<RE::SpellItem>(a_formID);
    }

    bool IsSupportedBeam(const RE::SpellItem* a_spell)
    {
        if (!a_spell || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
            return false;
        }
        if (a_spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration ||
            a_spell->GetDelivery() != RE::MagicSystem::Delivery::kAimed) {
            return false;
        }
        const auto formID = a_spell->GetFormID();
        return formID == kFlamesFormID || formID == kFrostbiteFormID || formID == kSparksFormID;
    }

    RE::SpellItem* CreateFusionSpell(RE::SpellItem* a_first, RE::SpellItem* a_second, const char* a_name)
    {
        if (!IsSupportedBeam(a_first) || !IsSupportedBeam(a_second) ||
            a_first->GetFormID() == a_second->GetFormID()) {
            return nullptr;
        }

        auto* duplicateForm = a_first->CreateDuplicateForm(false, nullptr);
        auto* fusion = duplicateForm ? duplicateForm->As<RE::SpellItem>() : nullptr;
        if (!fusion) {
            spdlog::error("Could not duplicate base spell {:08X}", a_first->GetFormID());
            return nullptr;
        }

        // Duplicate forms may own copied Effect objects or may initially alias source effects.
        // Delete only copied objects; never delete anything owned by the original spell.
        std::unordered_set<RE::Effect*> sourceEffects;
        for (auto* effect : a_first->effects) {
            if (effect) {
                sourceEffects.insert(effect);
            }
        }
        for (auto* copiedEffect : fusion->effects) {
            if (copiedEffect && !sourceEffects.contains(copiedEffect)) {
                delete copiedEffect;
            }
        }
        fusion->effects.clear();

        std::int32_t hostileCount = 0;
        std::size_t addedEffects = 0;
        const auto appendHalfEffects = [&](RE::SpellItem* a_source) {
            for (auto* sourceEffect : a_source->effects) {
                if (!sourceEffect || !sourceEffect->baseEffect) {
                    continue;
                }
                auto* combinedEffect = new RE::Effect();
                combinedEffect->effectItem = sourceEffect->effectItem;
                combinedEffect->effectItem.magnitude *= 0.5F;
                combinedEffect->baseEffect = sourceEffect->baseEffect;
                combinedEffect->cost = sourceEffect->cost;
                combinedEffect->pad0C = sourceEffect->pad0C;
                combinedEffect->pad1C = sourceEffect->pad1C;
                combinedEffect->conditions.Copy(&sourceEffect->conditions);
                if (combinedEffect->IsHostile()) {
                    ++hostileCount;
                }
                fusion->effects.push_back(combinedEffect);
                ++addedEffects;
            }
        };

        appendHalfEffects(a_first);
        appendHalfEffects(a_second);
        if (addedEffects < 2) {
            spdlog::error("Fusion {} rejected: only {} effects copied", a_name, addedEffects);
            return nullptr;
        }

        fusion->hostileCount = hostileCount;
        fusion->SetFullName(a_name);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !dataHandler->AddFormToDataHandler(fusion)) {
            spdlog::error("Could not register runtime fusion spell '{}'", a_name);
            return nullptr;
        }

        spdlog::info(
            "Created runtime fusion '{}' ({:08X}) from {:08X} + {:08X}; effects={}, hostile={}",
            a_name, fusion->GetFormID(), a_first->GetFormID(), a_second->GetFormID(), addedEffects, hostileCount);
        return fusion;
    }

    void BuildFusionSpells()
    {
        g_fusionSpells.clear();
        auto* fire = LookupVanillaSpell(kFlamesFormID);
        auto* frost = LookupVanillaSpell(kFrostbiteFormID);
        auto* shock = LookupVanillaSpell(kSparksFormID);

        if (!fire || !frost || !shock) {
            spdlog::error(
                "Required vanilla beam spell missing (Flames={}, Frostbite={}, Sparks={})",
                static_cast<const void*>(fire), static_cast<const void*>(frost), static_cast<const void*>(shock));
            return;
        }

        const auto add = [](RE::SpellItem* a, RE::SpellItem* b, const char* name) {
            if (auto* fusion = CreateFusionSpell(a, b, name)) {
                g_fusionSpells.emplace(MakePairKey(a->GetFormID(), b->GetFormID()), fusion);
            }
        };

        add(fire, frost, "Elemental Fusion - Fire and Frost");
        add(fire, shock, "Elemental Fusion - Fire and Shock");
        add(frost, shock, "Elemental Fusion - Frost and Shock");
        spdlog::info("Fusion spell cache contains {} of 3 combinations", g_fusionSpells.size());
    }

    RE::SpellItem* FindFusionSpell(RE::MagicItem* a_left, RE::MagicItem* a_right)
    {
        auto* left = a_left ? a_left->As<RE::SpellItem>() : nullptr;
        auto* right = a_right ? a_right->As<RE::SpellItem>() : nullptr;
        if (!IsSupportedBeam(left) || !IsSupportedBeam(right) ||
            left->GetFormID() == right->GetFormID()) {
            return nullptr;
        }
        const auto it = g_fusionSpells.find(MakePairKey(left->GetFormID(), right->GetFormID()));
        return it != g_fusionSpells.end() ? it->second : nullptr;
    }

    void CallOriginal(
        RE::AttackBlockHandler* a_handler,
        RE::ButtonEvent* a_event,
        RE::PlayerControlsData* a_data)
    {
        if (g_originalProcessButton.address() != 0) {
            g_originalProcessButton(a_handler, a_event, a_data);
        }
    }

    void DeliverSynthetic(
        RE::AttackBlockHandler* a_handler,
        const ButtonSnapshot& a_button,
        float a_value,
        float a_heldSeconds)
    {
        if (!a_handler || !a_button.data || g_originalProcessButton.address() == 0) {
            spdlog::error("Cannot deliver synthetic input for hand {}", a_button.hand);
            return;
        }

        auto* event = RE::ButtonEvent::Create(
            a_button.device, a_button.userEvent, a_button.idCode, a_value, a_heldSeconds);
        if (!event) {
            spdlog::error("ButtonEvent::Create returned null");
            return;
        }

        CallOriginal(a_handler, event, a_button.data);
        event->~ButtonEvent();
        RE::free(event);
    }

    std::size_t GetHand(const RE::BSFixedString& a_userEvent)
    {
        auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return 2;
        }
        if (a_userEvent == userEvents->leftAttack) {
            return RE::Actor::SlotTypes::kLeftHand;
        }
        if (a_userEvent == userEvents->rightAttack) {
            return RE::Actor::SlotTypes::kRightHand;
        }
        return 2;
    }

    ButtonSnapshot MakeSnapshot(
        std::size_t a_hand,
        const RE::ButtonEvent* a_event,
        RE::PlayerControlsData* a_data)
    {
        ButtonSnapshot result;
        result.hand = a_hand;
        result.device = a_event->device.get();
        result.userEvent = a_event->QUserEvent();
        result.idCode = a_event->GetIDCode();
        result.data = a_data;
        return result;
    }

    void ResetInputState()
    {
        g_input = InputState{};
    }

    void DeliverPendingAsSingle(RE::AttackBlockHandler* a_handler)
    {
        DeliverSynthetic(a_handler, g_input.pending, 1.0F, 0.0F);
        g_input.mode = InputMode::kSingleHand;
        g_input.singleHand = g_input.pending.hand;
    }

    void FinishPendingTap(
        RE::AttackBlockHandler* a_handler,
        const RE::ButtonEvent* a_releaseEvent)
    {
        const auto held = std::max(a_releaseEvent->heldDownSecs, kSyntheticReleaseHeldSeconds);
        DeliverSynthetic(a_handler, g_input.pending, 1.0F, 0.0F);
        DeliverSynthetic(a_handler, g_input.pending, 0.0F, held);
        ResetInputState();
    }

    bool TryBeginDual(
        RE::AttackBlockHandler* a_handler,
        RE::PlayerCharacter* a_player,
        const ButtonSnapshot& a_secondButton)
    {
        if (!a_handler || !a_player || g_input.mode != InputMode::kPendingFirstHand ||
            a_secondButton.hand == g_input.pending.hand) {
            return false;
        }

        auto& runtime = a_player->GetActorRuntimeData();
        auto* currentLeft = runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand];
        auto* currentRight = runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand];

        if (currentLeft != g_input.originalSpells[RE::Actor::SlotTypes::kLeftHand] ||
            currentRight != g_input.originalSpells[RE::Actor::SlotTypes::kRightHand]) {
            return false;
        }

        auto* fusion = FindFusionSpell(currentLeft, currentRight);
        if (!fusion) {
            return false;
        }

        ButtonSnapshot buttons[2]{};
        buttons[g_input.pending.hand] = g_input.pending;
        buttons[a_secondButton.hand] = a_secondButton;

        g_input.dualButtons[0] = buttons[0];
        g_input.dualButtons[1] = buttons[1];
        g_input.dualPhysicalDown = { true, true };
        g_input.fusionSpell = fusion;
        g_input.mode = InputMode::kDual;

        runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand] = fusion;
        runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand] = fusion;

        DeliverSynthetic(a_handler, g_input.dualButtons[RE::Actor::SlotTypes::kLeftHand], 1.0F, 0.0F);
        DeliverSynthetic(a_handler, g_input.dualButtons[RE::Actor::SlotTypes::kRightHand], 1.0F, 0.0F);

        spdlog::info(
            "Started dual fusion: left={:08X}, right={:08X}, combined={:08X}",
            currentLeft ? currentLeft->GetFormID() : 0,
            currentRight ? currentRight->GetFormID() : 0,
            fusion->GetFormID());
        return true;
    }

    void FinishDual(RE::AttackBlockHandler* a_handler, RE::PlayerCharacter* a_player)
    {
        DeliverSynthetic(a_handler, g_input.dualButtons[0], 0.0F, kSyntheticReleaseHeldSeconds);
        DeliverSynthetic(a_handler, g_input.dualButtons[1], 0.0F, kSyntheticReleaseHeldSeconds);

        if (a_player && g_input.fusionSpell) {
            auto& runtime = a_player->GetActorRuntimeData();
            if (runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand] == g_input.fusionSpell) {
                runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand] =
                    g_input.originalSpells[RE::Actor::SlotTypes::kLeftHand];
            }
            if (runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand] == g_input.fusionSpell) {
                runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand] =
                    g_input.originalSpells[RE::Actor::SlotTypes::kRightHand];
            }
        }

        LogInfo("Finished dual fusion and restored original hand selections");
        ResetInputState();
    }

    void ProcessButtonHook(
        RE::AttackBlockHandler* a_handler,
        RE::ButtonEvent* a_event,
        RE::PlayerControlsData* a_data)
    {
        if (!a_event || !a_handler || g_originalProcessButton.address() == 0) {
            CallOriginal(a_handler, a_event, a_data);
            return;
        }

        if (a_event->device.get() == RE::INPUT_DEVICE::kGamepad) {
            CallOriginal(a_handler, a_event, a_data);
            return;
        }

        const auto hand = GetHand(a_event->QUserEvent());
        if (hand > RE::Actor::SlotTypes::kRightHand) {
            CallOriginal(a_handler, a_event, a_data);
            return;
        }

        const bool isDown = a_event->value > 0.0F && a_event->heldDownSecs == 0.0F;
        const bool isHeld = a_event->value > 0.0F && a_event->heldDownSecs > 0.0F;
        const bool isUp = a_event->value == 0.0F && a_event->heldDownSecs > 0.0F;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            CallOriginal(a_handler, a_event, a_data);
            return;
        }

        switch (g_input.mode) {
        case InputMode::kIdle:
            if (isDown) {
                auto& runtime = player->GetActorRuntimeData();
                auto* left = runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand];
                auto* right = runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand];
                if (FindFusionSpell(left, right)) {
                    g_input.mode = InputMode::kPendingFirstHand;
                    g_input.pending = MakeSnapshot(hand, a_event, a_data);
                    g_input.originalSpells[RE::Actor::SlotTypes::kLeftHand] = left;
                    g_input.originalSpells[RE::Actor::SlotTypes::kRightHand] = right;
                    return;
                }
            }
            CallOriginal(a_handler, a_event, a_data);
            return;

        case InputMode::kPendingFirstHand:
            if (hand == g_input.pending.hand) {
                if (isHeld) {
                    if (a_event->heldDownSecs >= kDualPressWindow) {
                        DeliverPendingAsSingle(a_handler);
                        CallOriginal(a_handler, a_event, a_data);
                    }
                    return;
                }
                if (isUp) {
                    FinishPendingTap(a_handler, a_event);
                    return;
                }
                if (isDown) {
                    return;
                }
                return;
            }

            if (isDown) {
                const auto secondButton = MakeSnapshot(hand, a_event, a_data);
                if (TryBeginDual(a_handler, player, secondButton)) {
                    return;
                }
                DeliverPendingAsSingle(a_handler);
                CallOriginal(a_handler, a_event, a_data);
                return;
            }

            CallOriginal(a_handler, a_event, a_data);
            return;

        case InputMode::kSingleHand:
            if (hand == g_input.singleHand && isUp) {
                CallOriginal(a_handler, a_event, a_data);
                ResetInputState();
                return;
            }
            CallOriginal(a_handler, a_event, a_data);
            return;

        case InputMode::kDual:
            if (isDown) {
                return;
            }
            if (isHeld) {
                if (g_input.dualPhysicalDown[hand]) {
                    CallOriginal(a_handler, a_event, a_data);
                }
                return;
            }
            if (isUp) {
                g_input.dualPhysicalDown[hand] = false;
                if (!g_input.dualPhysicalDown[0] && !g_input.dualPhysicalDown[1]) {
                    FinishDual(a_handler, player);
                }
                return;
            }
            return;
        }

        CallOriginal(a_handler, a_event, a_data);
    }

    void InstallHook()
    {
        if (g_hookInstalled) {
            return;
        }
        REL::Relocation<std::uintptr_t> attackVtable{ RE::VTABLE_AttackBlockHandler[0] };
        g_originalProcessButton = attackVtable.write_vfunc(0x4, &ProcessButtonHook);
        g_hookInstalled = g_originalProcessButton.address() != 0;
        if (g_hookInstalled) {
            LogInfo("Installed AttackBlockHandler::ProcessButton hook");
        } else {
            spdlog::error("Failed to install AttackBlockHandler::ProcessButton hook");
        }
    }

    void OnDataLoaded()
    {
        BuildFusionSpells();
        if (!g_fusionSpells.empty()) {
            InstallHook();
        } else {
            spdlog::error("No fusion spells were created; input hook was not installed");
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    InitializeLogging();
    SKSE::Init(a_skse);

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_message) {
            if (a_message && a_message->type == SKSE::MessagingInterface::kDataLoaded) {
                OnDataLoaded();
            }
        })) {
        SKSE::stl::report_and_fail("ElementalFusionCasting: could not register SKSE messaging listener.");
    }

    LogInfo("Loaded. Waiting for Skyrim data to finish loading.");
    return true;
}
