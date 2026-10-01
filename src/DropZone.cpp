#include "DropZone.h"
#include "Strings.h"

#include "Common.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <File.h>
#include <Roster.h>

namespace {

const float kIconSize = 64;

} // namespace


DropZone::DropZone()
	:
	BView("drop zone", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fIcon(NULL),
	fHighlighted(false),
	fBusy(false)
{
}


DropZone::~DropZone()
{
	delete fIcon;
}


void
DropZone::AttachedToWindow()
{
	SetViewColor(B_TRANSPARENT_COLOR);

	// The app's own icon, drawn large in the middle.
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK) {
		BFile file(&info.ref, B_READ_ONLY);
		BAppFileInfo appInfo(&file);
		fIcon = new BBitmap(BRect(0, 0, kIconSize - 1, kIconSize - 1),
			B_RGBA32);
		if (appInfo.GetIcon(fIcon, (icon_size)kIconSize) != B_OK) {
			delete fIcon;
			fIcon = NULL;
		}
	}
}


BSize
DropZone::MinSize()
{
	return BSize(380, 190);
}


void
DropZone::FrameResized(float width, float height)
{
	Invalidate();
}


void
DropZone::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(panel);
	FillRect(bounds);

	BRect box = bounds.InsetByCopy(10, 10);
	rgb_color fill = fHighlighted
		? tint_color(ui_color(B_CONTROL_HIGHLIGHT_COLOR), B_LIGHTEN_1_TINT)
		: tint_color(panel, B_LIGHTEN_1_TINT);
	SetHighColor(fill);
	FillRoundRect(box, 8, 8);

	rgb_color edge = fHighlighted ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
		: tint_color(panel, B_DARKEN_2_TINT);
	SetHighColor(edge);
	SetPenSize(2);
	static const pattern kDashes = { { 0xf0, 0xf0, 0xf0, 0xf0, 0x0f, 0x0f,
		0x0f, 0x0f } };
	StrokeRoundRect(box, 8, 8, fHighlighted ? B_SOLID_HIGH : kDashes);
	SetPenSize(1);

	font_height fh;
	GetFontHeight(&fh);
	float lineHeight = fh.ascent + fh.descent + fh.leading;

	const char* lines[2];
	if (fBusy) {
		lines[0] = T("Working on it ...");
		lines[1] = "";
	} else {
		lines[0] = T("Drop a package (.hpkg) here to install it,");
		lines[1] = T("or a program or its folder to make a package.");
	}

	float contentHeight = (fIcon != NULL ? kIconSize + 12 : 0) + lineHeight * 2;
	float y = box.top + (box.Height() - contentHeight) / 2;
	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fIcon, BPoint(box.left + (box.Width() - kIconSize) / 2, y));
		SetDrawingMode(B_OP_COPY);
		y += kIconSize + 12;
	}
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	SetLowColor(fill);
	for (int i = 0; i < 2; i++) {
		float width = StringWidth(lines[i]);
		DrawString(lines[i], BPoint(box.left + (box.Width() - width) / 2,
			y + fh.ascent));
		y += lineHeight;
	}
}


void
DropZone::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	bool highlight = !fBusy && dragMessage != NULL
		&& dragMessage->HasRef("refs")
		&& (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW);
	if (highlight != fHighlighted) {
		fHighlighted = highlight;
		Invalidate();
	}
}


void
DropZone::MouseUp(BPoint where)
{
	if (fHighlighted) {
		fHighlighted = false;
		Invalidate();
	}
}


void
DropZone::SetBusy(bool busy)
{
	fBusy = busy;
	fHighlighted = false;
	Invalidate();
}
