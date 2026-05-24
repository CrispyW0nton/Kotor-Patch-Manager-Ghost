void main() {
    object oPC = GetFirstPC();
    if (!GetIsObjectValid(oPC)) {
        return;
    }

    AssignCommand(oPC, ClearAllActions());
    AssignCommand(oPC, ActionPlayAnimation(65000, 1.0, 10.0));
}
