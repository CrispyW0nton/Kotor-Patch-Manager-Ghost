void main() {
    object oEntering = GetEnteringObject();
    if (!GetIsPC(oEntering)) {
        return;
    }

    AssignCommand(oEntering, ClearAllActions());
    DelayCommand(1.0, AssignCommand(oEntering, ActionPlayAnimation(65000, 1.0, 10.0)));
}
