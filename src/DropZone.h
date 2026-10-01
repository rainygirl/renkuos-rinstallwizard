#ifndef RIW_DROP_ZONE_H
#define RIW_DROP_ZONE_H

#include <View.h>

class BBitmap;

// The big target at the top of the main window. Drops land in the window's
// MessageReceived (the view passes them up); this only draws and lights up
// while something is dragged over it.
class DropZone : public BView {
public:
	DropZone();
	~DropZone() override;

	void AttachedToWindow() override;
	void Draw(BRect updateRect) override;
	void FrameResized(float width, float height) override;
	void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage) override;
	void MouseUp(BPoint where) override;
	BSize MinSize() override;

	void SetBusy(bool busy);

private:
	BBitmap* fIcon;
	bool fHighlighted;
	bool fBusy;
};

#endif
