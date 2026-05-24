void main() {
    object oPC = GetFirstPC();
    if (!GetIsObjectValid(oPC)) {
        return;
    }

    DelayCommand(1.0, AssignCommand(oPC, ClearAllActions()));
    DelayCommand(1.2, AssignCommand(oPC, ActionPlayAnimation(65000, 1.0, 10.0)));
}
