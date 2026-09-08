/*
* This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "gilneas.h"
#include "Containers.h"
#include "ScriptMgr.h"
#include "CombatAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PassiveAI.h"
#include "PhasingHandler.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

namespace Gilneas::Chapter1
{

/*######
## Quest 14098 - Evacuate the Merchant Square
######*/

enum FrightenedCitizen
{
    SAY_FRIGHTENED_CITIZEN_RESCUE   = 0,

    NPC_EVACUATION_STALKER_FIRST    = 35830,
    NPC_EVACUATION_STALKER_NEAR     = 35010,
    NPC_EVACUATION_STALKER_FAR      = 35011,
    NPC_RAMPAGING_WORGEN            = 35660,

    CREDIT_35830                    = 35830,

    POINT_STALKER_FIRST             = 1,
    POINT_STALKER_NEAR              = 2,
    POINT_STALKER_FAR               = 3,

    EVENT_TALK_FRIGHTENED           = 1,
    EVENT_MOVE_TO_NEAR_STALKER      = 2,
    EVENT_MOVE_TO_FAR_STALKER       = 3,
    EVENT_DESPAWN                   = 4
};

struct npc_frightened_citizen : public PassiveAI
{
    npc_frightened_citizen(Creature* creature) : PassiveAI(creature) { }

    void IsSummonedBy(Unit* /*summoner*/) override
    {
        if (Creature* stalkerNear = me->FindNearestCreature(NPC_EVACUATION_STALKER_FIRST, 20.0f))
            me->GetMotionMaster()->MovePoint(POINT_STALKER_FIRST, stalkerNear->GetPosition(), true);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        switch (id)
        {
            case POINT_STALKER_FIRST:
                _events.ScheduleEvent(EVENT_TALK_FRIGHTENED, 1s);
                break;
            case POINT_STALKER_NEAR:
                _events.ScheduleEvent(EVENT_MOVE_TO_FAR_STALKER, 1ms);
                break;
            case POINT_STALKER_FAR:
                _events.ScheduleEvent(EVENT_DESPAWN, 1ms);
                break;
            default:
                break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TALK_FRIGHTENED:
                    if (Unit* summoner = me->ToTempSummon()->GetSummoner())
                    {
                        if (Player* player = summoner->ToPlayer())
                        {
                            player->KilledMonsterCredit(CREDIT_35830);
                            Talk(SAY_FRIGHTENED_CITIZEN_RESCUE, summoner);
                        }
                    }
                    _events.ScheduleEvent(EVENT_MOVE_TO_NEAR_STALKER, 2s);
                    break;
                case EVENT_MOVE_TO_NEAR_STALKER:
                    if (Creature* stalker = me->FindNearestCreature(NPC_EVACUATION_STALKER_FAR, 50.0f))
                        me->GetMotionMaster()->MovePoint(POINT_STALKER_FAR, stalker->GetPosition(), true);
                    else if (Creature* stalker = me->FindNearestCreature(NPC_EVACUATION_STALKER_NEAR, 100.0f))
                        me->GetMotionMaster()->MovePoint(POINT_STALKER_NEAR, stalker->GetPosition(), true);
                    break;
                case EVENT_MOVE_TO_FAR_STALKER:
                    if (Creature* stalker = me->FindNearestCreature(NPC_EVACUATION_STALKER_FAR, 500.0f))
                        me->GetMotionMaster()->MovePoint(POINT_STALKER_FAR, stalker->GetPosition(), true);
                    break;
                case EVENT_DESPAWN:
                    me->DespawnOrUnsummon();
                    break;
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
};

/*######
## Quest 14154 - By the Skin of his Teeth
######*/

enum ByTheSkinOfHisTeeth
{
    QUEST_BY_THE_SKIN_OF_HIS_TEETH  = 14154,

    DATA_QUEST_OWNER                = 1,

    NPC_WORGEN_RUNT_SPELL           = 35188,
    NPC_WORGEN_ALPHA                = 35167,
    NPC_WORGEN_RUNT                 = 35456,
    NPC_BLOODFANG_BLOODLETTER       = 35457,

    SPELL_SUMMON_RAVENOUS_WORGEN_1  = 66836,
    SPELL_SUMMON_RAVENOUS_WORGEN_2  = 66925,
    SPELL_GILNEAS_PRISON_FORCECAST  = 66914,
    SPELL_GILNEAS_PRISON_PERIODIC   = 66894,
    SPELL_BY_THE_SKIN_OF_HIS_TEETH  = 68218,
    SPELL_LEFT_HOOK                 = 67825,
    SPELL_TAUNT                     = 37548,
    SPELL_ENRAGE                    = 8599,

    EVENT_JUMP_TO_PRISON            = 1,
    EVENT_AGGRO_PLAYER,
    EVENT_CHECK_OWNER,
    EVENT_FORCE_DESPAWN,

    EVENT_TAUNT_ATTACKERS           = 1,
    EVENT_LEFT_HOOK,

    POINT_ROUTE_END                 = 99,
    POINT_SPELL_SUMMON_LAND         = 100,
    POINT_PRISON_LAND               = 101,

    MAX_WORGEN_ROUTES               = 15,
    FIRST_CATHEDRAL_ROUTE           = 7,
    MAX_ACTIVE_WORGEN_PER_PLAYER    = 6
};

struct WorgenRoute
{
    Position const* Path;
    uint32 Size;
};

std::array<WorgenRoute, MAX_WORGEN_ROUTES> const WorgenRoutes =
{{
    { worgenRuntHousePath1, runtHousePathSize1 },
    { worgenRuntHousePath2, runtHousePathSize2 },
    { worgenRuntHousePath3, runtHousePathSize3 },
    { worgenRuntHousePath4, runtHousePathSize4 },
    { worgenRuntHousePath5, runtHousePathSize5 },
    { worgenRuntHousePath6, runtHousePathSize6 },
    { worgenRuntHousePath7, runtHousePathSize7 },
    { worgenRuntCathedralPath1, runtCathedralPathSize1 },
    { worgenRuntCathedralPath2, runtCathedralPathSize2 },
    { worgenRuntCathedralPath3, runtCathedralPathSize3 },
    { worgenRuntCathedralPath4, runtCathedralPathSize4 },
    { worgenRuntCathedralPath5, runtCathedralPathSize5 },
    { worgenRuntCathedralPath6, runtCathedralPathSize6 },
    { worgenRuntCathedralPath7, runtCathedralPathSize7 },
    { worgenRuntCathedralPath8, runtCathedralPathSize8 }
}};

std::array<Position, MAX_WORGEN_ROUTES> const WorgenSpawnPositions =
{{
    { -1729.345f, 1526.495f, 55.47962f, 6.188943f },
    { -1709.63f, 1527.464f, 56.86086f, 3.258752f },
    { -1717.75f, 1513.727f, 55.47941f, 4.704845f },
    { -1724.719f, 1526.731f, 55.66177f, 6.138319f },
    { -1713.974f, 1526.625f, 56.21981f, 3.306195f },
    { -1718.104f, 1524.071f, 55.81641f, 4.709816f },
    { -1718.262f, 1518.557f, 55.55954f, 4.726997f },
    { -1618.054f, 1489.644f, 68.45153f, 3.593639f },
    { -1625.62f, 1487.033f, 71.27762f, 3.531424f },
    { -1638.569f, 1489.736f, 68.55273f, 4.548815f },
    { -1630.399f, 1481.66f, 71.41516f, 3.484555f },
    { -1622.424f, 1483.882f, 67.67381f, 3.404875f },
    { -1634.344f, 1491.3f, 70.10101f, 4.6248f },
    { -1631.979f, 1491.585f, 71.11481f, 4.032866f },
    { -1627.273f, 1499.689f, 68.89395f, 4.251452f }
}};

static Position const RuntSpellSummonJumpPos = { -1671.915f, 1446.734f, 52.28712f };

struct npc_worgen_runt : public ScriptedAI
{
    npc_worgen_runt(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        std::list<Creature*> worgen;
        for (uint32 entry : { NPC_WORGEN_RUNT_SPELL, NPC_WORGEN_ALPHA, NPC_WORGEN_RUNT, NPC_BLOODFANG_BLOODLETTER })
            summoner->GetCreatureListWithEntryInGrid(worgen, entry, 150.0f);

        uint32 activeWorgen = 0;
        for (Creature* attacker : worgen)
            if (attacker != me && attacker->IsAlive() && attacker->IsAIEnabled()
                && attacker->AI()->GetGUID(DATA_QUEST_OWNER) == summoner->GetGUID())
                ++activeWorgen;

        if (activeWorgen >= MAX_ACTIVE_WORGEN_PER_PLAYER)
        {
            me->DespawnOrUnsummon();
            return;
        }

        me->setActive(true); // we are in a phased and cut off map so we're fine to use that here
        me->SetReactState(REACT_PASSIVE);
        _playerGuid = summoner->GetGUID();
        _events.ScheduleEvent(EVENT_CHECK_OWNER, 1s);
        _events.ScheduleEvent(EVENT_FORCE_DESPAWN, 70s);

        uint32 summonSpell = me->GetUInt32Value(UNIT_CREATED_BY_SPELL);
        if (summonSpell == SPELL_SUMMON_RAVENOUS_WORGEN_1 || summonSpell == SPELL_SUMMON_RAVENOUS_WORGEN_2)
        {
            me->GetMotionMaster()->MoveJump(RuntSpellSummonJumpPos, 16.0f, 4.371286f, POINT_SPELL_SUMMON_LAND);
            me->SetHomePosition(RuntSpellSummonJumpPos);
        }
    }

    void DoAction(int32 action) override
    {
        if (_playerGuid.IsEmpty() || action < 0 || action >= MAX_WORGEN_ROUTES)
            return;

        _routeId = uint8(action);
        WorgenRoute const& route = WorgenRoutes[_routeId];
        me->GetMotionMaster()->MoveSmoothPath(POINT_ROUTE_END, route.Path, route.Size - 1);
    }

    ObjectGuid GetGUID(int32 data) const override
    {
        return data == DATA_QUEST_OWNER ? _playerGuid : ObjectGuid::Empty;
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (me->GetEntry() == NPC_WORGEN_ALPHA && !_enraged && me->HealthBelowPctDamaged(30, damage))
        {
            _enraged = true;
            DoCastSelf(SPELL_ENRAGE, true);
            Talk(0);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(5s);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != EFFECT_MOTION_TYPE)
            return;

        if (pointId == POINT_SPELL_SUMMON_LAND && !_landed)
        {
            _landed = true;
            _events.ScheduleEvent(EVENT_AGGRO_PLAYER, 1ms);
            return;
        }

        if (pointId == POINT_ROUTE_END && !_jumped && _routeId < MAX_WORGEN_ROUTES)
        {
            _jumped = true;
            _events.ScheduleEvent(EVENT_JUMP_TO_PRISON, 1ms);
        }
        else if (pointId == POINT_PRISON_LAND && _jumped && !_landed)
        {
            _landed = true;
            _events.ScheduleEvent(EVENT_AGGRO_PLAYER, 1ms);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FORCE_DESPAWN:
                    me->DespawnOrUnsummon();
                    break;
                case EVENT_JUMP_TO_PRISON:
                    me->GetMotionMaster()->MoveJump(worgenRuntJumpPos[_routeId], 16.0f, _routeId < FIRST_CATHEDRAL_ROUTE ? 19.2911f : frand(3.945607f, 4.852813f), POINT_PRISON_LAND);
                    me->SetHomePosition(worgenRuntJumpPos[_routeId]);
                    break;
                case EVENT_AGGRO_PLAYER:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                        if (player->IsAlive() && player->HasAura(SPELL_BY_THE_SKIN_OF_HIS_TEETH) && me->IsWithinDistInMap(player, 150.0f))
                        {
                            me->SetReactState(REACT_AGGRESSIVE);
                            AttackStart(player);
                        }
                    break;
                case EVENT_CHECK_OWNER:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                    {
                        if (!player->IsAlive() || !player->HasAura(SPELL_BY_THE_SKIN_OF_HIS_TEETH) || !me->IsWithinDistInMap(player, 150.0f))
                            me->DespawnOrUnsummon();
                        else
                            _events.ScheduleEvent(EVENT_CHECK_OWNER, 1s);
                    }
                    else
                        me->DespawnOrUnsummon();
                    break;
                default:
                    break;
            }
        }

        if (_landed && UpdateVictim())
            DoMeleeAttackIfReady();
    }
private:
    uint8 _routeId = MAX_WORGEN_ROUTES;
    bool _jumped = false;
    bool _landed = false;
    bool _enraged = false;
    ObjectGuid _playerGuid;
    EventMap _events;
};

struct npc_lord_darius_crowley : public ScriptedAI
{
    npc_lord_darius_crowley(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);
    }

    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_TAUNT_ATTACKERS, 2s);
    }

    void QuestAccept(Player* player, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_BY_THE_SKIN_OF_HIS_TEETH)
            me->CastSpell(player, SPELL_GILNEAS_PRISON_FORCECAST, true);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_LEFT_HOOK, 6s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TAUNT_ATTACKERS:
                    TauntAttackers();
                    _events.ScheduleEvent(EVENT_TAUNT_ATTACKERS, 3s);
                    break;
                case EVENT_LEFT_HOOK:
                    if (me->GetVictim())
                        DoCastVictim(SPELL_LEFT_HOOK);
                    _events.ScheduleEvent(EVENT_LEFT_HOOK, 15s + Milliseconds(urand(0, 2000)));
                    break;
                default:
                    break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    void TauntAttackers()
    {
        std::list<Creature*> attackers;
        for (uint32 entry : { NPC_WORGEN_RUNT_SPELL, NPC_WORGEN_ALPHA, NPC_WORGEN_RUNT, NPC_BLOODFANG_BLOODLETTER })
            me->GetCreatureListWithEntryInGrid(attackers, entry, 100.0f);

        for (Creature* attacker : attackers)
        {
            if (!attacker->IsAIEnabled())
                continue;

            if (Player* owner = ObjectAccessor::GetPlayer(*me, attacker->AI()->GetGUID(DATA_QUEST_OWNER)))
                if (owner->IsAlive() && owner->HasAura(SPELL_BY_THE_SKIN_OF_HIS_TEETH) && attacker->GetVictim() == owner)
                    me->CastSpell(attacker, SPELL_TAUNT, true);
        }
    }

    EventMap _events;
};

class spell_gen_gilneas_prison_periodic_dummy : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SUMMON_RAVENOUS_WORGEN_1,
            SPELL_SUMMON_RAVENOUS_WORGEN_2,
            SPELL_GILNEAS_PRISON_FORCECAST,
            SPELL_GILNEAS_PRISON_PERIODIC,
            SPELL_BY_THE_SKIN_OF_HIS_TEETH
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetHitPlayer();
        if (!player)
            return;

        player->CastSpell(player, RAND(SPELL_SUMMON_RAVENOUS_WORGEN_1, SPELL_SUMMON_RAVENOUS_WORGEN_2), true);

        std::array<uint8, MAX_WORGEN_ROUTES> routes;
        for (uint8 routeId = 0; routeId < MAX_WORGEN_ROUTES; ++routeId)
            routes[routeId] = routeId;
        Trinity::Containers::RandomShuffle(routes);

        bool summonAlphas = false;
        if (Aura* aura = player->GetAura(SPELL_GILNEAS_PRISON_PERIODIC))
            summonAlphas = aura->GetDuration() <= 30 * IN_MILLISECONDS;

        if (summonAlphas)
            SummonAttacker(player, NPC_WORGEN_ALPHA, routes[0]);
        else
        {
            SummonAttacker(player, NPC_WORGEN_RUNT, routes[0]);
            SummonAttacker(player, NPC_WORGEN_RUNT, routes[1]);
            SummonAttacker(player, NPC_BLOODFANG_BLOODLETTER, routes[2]);
        }
    }

    static void SummonAttacker(Unit* summoner, uint32 entry, uint8 routeId)
    {
        if (Creature* attacker = summoner->SummonCreature(entry, WorgenSpawnPositions[routeId]))
            attacker->AI()->DoAction(routeId);
    }

    void Register() override
    {
        OnEffectHitTarget.Register(&spell_gen_gilneas_prison_periodic_dummy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/*######
## Quest 14159 - The Rebel Lord's Arsenal
######*/

enum TheRebelLordsArsena
{
    PHASE_ID_SUMMON         = 170,
    PHASE_ID_WOUND          = 171,
    NPC_LORNA_CROWLEY       = 35378,
    NPC_GENERIC_TRIGGER_LAB = 35374,

    SPELL_SHOOT_INSTAKILL   = 67593,
    SPELL_COSMETIC_ATTACK   = 42880,
    SPELL_PULL_TO           = 67357,

    EVENT_COSMETIC_ATTACK   = 1,
    EVENT_JUMP_TO_PLAYER,
    EVENT_SHOOT_JOSIAH
};

static Position const JosiahJumpPos = { -1796.63f, 1427.73f, 12.4624f };

struct npc_josiah_avery : public PassiveAI
{
    npc_josiah_avery(Creature* creature) : PassiveAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        PhasingHandler::AddPhase(me, PHASE_ID_SUMMON, true);
        PhasingHandler::AddPhase(me, PHASE_ID_WOUND, true);
        _playerGuid = summoner->GetGUID();
        _events.ScheduleEvent(EVENT_COSMETIC_ATTACK, 500ms);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(5s);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_COSMETIC_ATTACK:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                    {
                        DoCast(player, SPELL_COSMETIC_ATTACK);
                        if (Creature* lorna = me->FindNearestCreature(NPC_LORNA_CROWLEY, 30.0f, true))
                            if (Creature* labTrigger = lorna->FindNearestCreature(NPC_GENERIC_TRIGGER_LAB, 5.0f, true))
                                labTrigger->CastSpell(player, SPELL_PULL_TO);

                        _events.ScheduleEvent(EVENT_JUMP_TO_PLAYER, 1s);
                    }
                    break;
                case EVENT_JUMP_TO_PLAYER:
                    me->GetMotionMaster()->MoveJump(JosiahJumpPos, 10.0f, 14.18636f);
                    _events.ScheduleEvent(EVENT_SHOOT_JOSIAH, 500ms);
                    break;
                case EVENT_SHOOT_JOSIAH:
                    if (Creature* lorna = me->FindNearestCreature(NPC_LORNA_CROWLEY, 30.0f, true))
                        lorna->CastSpell(me, SPELL_SHOOT_INSTAKILL, true);
                    break;
                default:
                    break;
            }
        }
    }
private:
    ObjectGuid _playerGuid;
    EventMap _events;
};

/*######
## Quest 14293 - Save Krennan Aranas
######*/

static Position const GreymanesHorseJumpPos = { -1676.16f, 1346.19f, 15.1349f };

enum GreymanesHorse
{
    SAY_ANNOUNCE_RESCUE     = 0,
    SAY_RESCUED             = 0,
    SAY_TRAPPED             = 0,

    PATH_KRENNAN_TREE       = 0,
    PATH_KRENNAN_BACK       = 1,

    EVENT_START_PATH_1      = 1,
    EVENT_START_PATH_2,
    EVENT_JUMP_TO_KRENNAN,
    EVENT_ANNOUNCE_RESCUE,
    EVENT_DISMOUNT_PLAYER,

    NPC_RESCUED_KRENNAN     = 35907,
    NPC_TRAPPED_KRENNAN     = 35753,
};

struct npc_greymanes_horse : public VehicleAI
{
    npc_greymanes_horse(Creature* creature) : VehicleAI(creature), _currentPath(PATH_KRENNAN_TREE) { }

    void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
    {
        if (apply && passenger->GetTypeId() == TYPEID_PLAYER)
        {
            me->SetControlled(true, UNIT_STATE_ROOT);
            _events.ScheduleEvent(EVENT_START_PATH_1, 1s);
        }
        else if (apply && passenger->GetEntry() == NPC_RESCUED_KRENNAN)
            _events.ScheduleEvent(EVENT_START_PATH_2, 1s);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type == EFFECT_MOTION_TYPE && pointId == pathSize1 && _currentPath == PATH_KRENNAN_TREE)
            _events.ScheduleEvent(EVENT_JUMP_TO_KRENNAN, 1ms);
        else if (type == EFFECT_MOTION_TYPE && pointId == pathSize2 && _currentPath == PATH_KRENNAN_BACK)
            _events.ScheduleEvent(EVENT_DISMOUNT_PLAYER, 1ms);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_START_PATH_1:
                    me->SetControlled(false, UNIT_STATE_ROOT);
                    _currentPath = PATH_KRENNAN_TREE;
                    me->GetMotionMaster()->MoveSmoothPath(pathSize1, greymanesHorsePath1, pathSize1);
                    break;
                case EVENT_JUMP_TO_KRENNAN:
                    me->GetMotionMaster()->MoveJump(GreymanesHorseJumpPos, 16.0f, 14.00968f);
                    _events.ScheduleEvent(EVENT_ANNOUNCE_RESCUE, 2s);
                    break;
                case EVENT_ANNOUNCE_RESCUE:
                    me->SetControlled(true, UNIT_STATE_ROOT);
                    if (Unit* passenger = me->GetVehicleKit()->GetPassenger(0))
                    {
                        Talk(SAY_ANNOUNCE_RESCUE, passenger);
                        if (Creature* krennan = me->FindNearestCreature(NPC_TRAPPED_KRENNAN, 30.0f, true))
                            krennan->AI()->Talk(SAY_TRAPPED, passenger);
                    }
                    break;
                case EVENT_START_PATH_2:
                    me->SetControlled(true, UNIT_STATE_ROOT);
                    _currentPath = PATH_KRENNAN_BACK;
                    me->GetMotionMaster()->MoveSmoothPath(pathSize2, greymanesHorsePath2, pathSize2);
                    break;
                case EVENT_DISMOUNT_PLAYER:
                    if (Unit* passenger = me->GetVehicleKit()->GetPassenger(1))
                    {
                        if (Unit* player = me->GetVehicleKit()->GetPassenger(0))
                        {
                            if (Creature* krennan = passenger->ToCreature())
                            {
                                std::vector<Unit*> storedAttackers;

                                for (Unit* attacker : me->getAttackers())
                                    storedAttackers.push_back(attacker);

                                for (Unit* attacker : storedAttackers)
                                {
                                    if (Creature* creature = attacker->ToCreature())
                                        if (creature->IsAIEnabled())
                                            creature->AI()->EnterEvadeMode();
                                }

                                player->ExitVehicle();
                                krennan->AddUnitState(UNIT_STATE_ROOT);
                                krennan->ExitVehicle();
                                krennan->AI()->Talk(SAY_RESCUED, me);
                                krennan->DespawnOrUnsummon(7s);
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
    uint32 _currentPath;
};

/*######
## Quest 14212 - Sacrifices
######*/

enum CrowleysHorse
{
    EVENT_JUMP_OVER_BARRICADES_1 = 1,
    EVENT_JUMP_OVER_BARRICADES_2,
    EVENT_MOVE_PATH_MAIN_1,
    EVENT_MOVE_PATH_MAIN_2,
    EVENT_MOVE_OFF_PATH = 6,

    PATH_ID_CROWLEYS_HORSE_1    = 352310,
    PATH_ID_CROWLEYS_HORSE_2    = 352311,
    PATH_ID_CROWLEYS_HORSE_3    = 444280,

    SPELL_THROW_TORCH           = 67063,
    NPC_CROWLEYS_HORSE_2        = 44428,
};

static Position const CrowleysHorseJumpPos = { -1714.762f, 1673.16f, 20.49182f };
static Position const CrowleysHorseJumpPos2 = { -1566.71f, 1708.04f, 20.4849f };

struct npc_crowleys_horse : public VehicleAI
{
    npc_crowleys_horse(Creature* creature) : VehicleAI(creature), _currentPath(0) { }

    void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
    {
        if (apply && passenger->GetTypeId() == TYPEID_PLAYER && me->GetEntry() != NPC_CROWLEYS_HORSE_2)
        {
            me->SetControlled(true, UNIT_STATE_ROOT);
            _events.ScheduleEvent(EVENT_JUMP_OVER_BARRICADES_1, 2s);
        }
        else if (apply && passenger->GetTypeId() == TYPEID_PLAYER)
        {
            me->SetControlled(true, UNIT_STATE_ROOT);
            _events.ScheduleEvent(EVENT_MOVE_OFF_PATH, 2s);
        }
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type == WAYPOINT_MOTION_TYPE && pointId == 14 && _currentPath == PATH_ID_CROWLEYS_HORSE_1)
            _events.ScheduleEvent(EVENT_JUMP_OVER_BARRICADES_2, 1s);
        else if (type == WAYPOINT_MOTION_TYPE && pointId == 15 && _currentPath == PATH_ID_CROWLEYS_HORSE_2)
            _events.ScheduleEvent(EVENT_DISMOUNT_PLAYER, 1ms);
        else if (type == WAYPOINT_MOTION_TYPE && pointId == 16 && _currentPath == PATH_ID_CROWLEYS_HORSE_3)
            _events.ScheduleEvent(EVENT_DISMOUNT_PLAYER, 1ms);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_JUMP_OVER_BARRICADES_1:
                    me->SetControlled(false, UNIT_STATE_ROOT);
                    me->GetMotionMaster()->MoveJump(CrowleysHorseJumpPos, 16.0f, 18.56182f);
                    _events.ScheduleEvent(EVENT_MOVE_PATH_MAIN_1, 2s);
                    break;
                case EVENT_MOVE_PATH_MAIN_1:
                    me->GetMotionMaster()->MovePath(PATH_ID_CROWLEYS_HORSE_1, false);
                    _currentPath = PATH_ID_CROWLEYS_HORSE_1;
                    break;
                case EVENT_JUMP_OVER_BARRICADES_2:
                    me->GetMotionMaster()->MoveJump(CrowleysHorseJumpPos2, 16.0f, 18.56182f);
                    _events.ScheduleEvent(EVENT_MOVE_PATH_MAIN_2, 2s);
                    break;
                case EVENT_MOVE_PATH_MAIN_2:
                    _currentPath = PATH_ID_CROWLEYS_HORSE_2;
                    me->GetMotionMaster()->MovePath(PATH_ID_CROWLEYS_HORSE_2, false);
                    break;
                case EVENT_DISMOUNT_PLAYER:
                {
                    std::set<Unit*> attackersCopy = me->getAttackers();
                    for (Unit* attacker : attackersCopy)
                    {
                        if (Creature* creature = attacker->ToCreature())
                            if (creature->IsAIEnabled())
                                creature->AI()->EnterEvadeMode();
                    }

                    me->RemoveAurasDueToSpell(VEHICLE_SPELL_RIDE_HARDCODED);
                    me->DespawnOrUnsummon(5s);
                    break;
                }
                case EVENT_MOVE_OFF_PATH:
                    me->SetControlled(false, UNIT_STATE_ROOT);
                    _currentPath = PATH_ID_CROWLEYS_HORSE_3;
                    me->GetMotionMaster()->MovePath(PATH_ID_CROWLEYS_HORSE_3, false);
                    break;
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
    uint32 _currentPath;
};

enum GileanCrow
{
    SPELL_GILNEAN_CROW      = 93275,
    EVENT_APPLY_HOVER_BYTES = 1,
    EVENT_FLY_AWAY_1        = 2,
    EVENT_FLY_AWAY_2        = 3,

    POINT_NONE              = 0,
    POINT_CROW_FLIGHT       = 1
};

struct npc_gilnean_crow : public PassiveAI
{
    npc_gilnean_crow(Creature* creature) : PassiveAI(creature) { }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spell) override
    {
        if (spell->Id == SPELL_GILNEAN_CROW)
            _events.ScheduleEvent(EVENT_APPLY_HOVER_BYTES, 500ms);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type == POINT_MOTION_TYPE && pointId == POINT_CROW_FLIGHT)
        {
            Position pos = me->GetRandomNearPosition(8.0f);
            pos.m_positionZ = me->GetPositionZ() + 10.0f;
            me->GetMotionMaster()->MovePoint(POINT_NONE, pos);
            me->DespawnOrUnsummon(14s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_APPLY_HOVER_BYTES:
                    me->SetStandState(UNIT_STAND_STATE_SIT);
                    me->SetAnimTier(AnimTier::Submerged);
                    _events.ScheduleEvent(EVENT_FLY_AWAY_1, 1s);
                    break;
                case EVENT_FLY_AWAY_1:
                {
                    Position pos = me->GetRandomNearPosition(7.0f);
                    pos.m_positionZ = me->GetPositionZ() + 8.0f;
                    me->GetMotionMaster()->MovePoint(POINT_CROW_FLIGHT, pos);
                    break;
                }
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
};
}

void AddSC_gilneas_chapter_1()
{
    using namespace Gilneas::Chapter1;
    RegisterCreatureAI(npc_frightened_citizen);
    RegisterCreatureAI(npc_worgen_runt);
    RegisterCreatureAI(npc_lord_darius_crowley);
    RegisterCreatureAI(npc_josiah_avery);
    RegisterCreatureAI(npc_greymanes_horse);
    RegisterCreatureAI(npc_crowleys_horse);
    RegisterCreatureAI(npc_gilnean_crow);
    RegisterSpellScript(spell_gen_gilneas_prison_periodic_dummy);
}
