-- By the Skin of His Teeth (Quest 14154)
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'npc_lord_darius_crowley' WHERE `entry` = 35077;
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'npc_worgen_runt' WHERE `entry` IN (35167, 35188, 35456, 35457);

DELETE FROM `smart_scripts` WHERE `entryorguid` IN (35077, 35167, 35188, 35456, 35457) AND `source_type` = 0;

DELETE FROM `spell_script_names` WHERE `spell_id` = 66853;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
    (66853, 'spell_gen_gilneas_prison_periodic_dummy');
