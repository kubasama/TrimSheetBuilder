#pragma once
// Drag-to-change number fields, as found in many 3D tools: press on a spin
// box and drag left / right to change the value. A click without dragging
// edits the text.
//   Shift while dragging: 10x finer.  Ctrl: snap to bigger steps.
// Applied to every spin box in the app (see Theme).

class QAbstractSpinBox;

namespace DragValue {

// Value change per pixel can be set per widget with the dynamic property
// "dragStep" (double). Default: half a single step per pixel.
void enable(QAbstractSpinBox *spin);

} // namespace DragValue
