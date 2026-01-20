// Scintilla source code edit control
/** @file PerLine.cxx
 ** Manages data associated with each line of the document
 **/
// Copyright 1998-2009 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>

#include <stdexcept>
#include <string_view>
#include <vector>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <memory>

#include "ScintillaTypes.h"
#include "ScintillaStructures.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "CellBuffer.h"
#include "PerLine.h"

using namespace Scintilla::Internal;

MarkerHandleSet::MarkerHandleSet() {
}

bool MarkerHandleSet::Empty() const noexcept {
	return mhList.empty();
}

int MarkerHandleSet::MarkValue() const noexcept {
	unsigned int m = 0;
	for (const MarkerHandleNumber &mhn : mhList) {
		m |= (1 << mhn.number);
	}
	return m;
}

bool MarkerHandleSet::Contains(int handle) const noexcept {
	for (const MarkerHandleNumber &mhn : mhList) {
		if (mhn.handle == handle) {
			return true;
		}
	}
	return false;
}

MarkerHandleNumber const *MarkerHandleSet::GetMarkerHandleNumber(int which) const noexcept {
	for (const MarkerHandleNumber &mhn : mhList) {
		if (which == 0)
			return &mhn;
		which--;
	}
	return nullptr;
}

bool MarkerHandleSet::InsertHandle(int handle, int markerNum) {
	mhList.push_front(MarkerHandleNumber(handle, markerNum));
	return true;
}

void MarkerHandleSet::RemoveHandle(int handle) {
	mhList.remove_if([handle](const MarkerHandleNumber &mhn) noexcept { return mhn.handle == handle; });
}

bool MarkerHandleSet::RemoveNumber(int markerNum, bool all) {
	bool performedDeletion = false;
	mhList.remove_if([&](const MarkerHandleNumber &mhn) noexcept {
		if ((all || !performedDeletion) && (mhn.number == markerNum)) {
			performedDeletion = true;
			return true;
		}
		return false;
	});
	return performedDeletion;
}

void MarkerHandleSet::CombineWith(MarkerHandleSet *other) noexcept {
	mhList.splice_after(mhList.before_begin(), other->mhList);
}

void LineMarkers::Init() {
	markers.DeleteAll();
}

void LineMarkers::InsertLine(Sci::Line line) {
	if (markers.Length()) {
		markers.Insert(line, nullptr);
	}
}

void LineMarkers::InsertLines(Sci::Line line, Sci::Line lines) {
	if (markers.Length()) {
		markers.InsertEmpty(line, lines);
	}
}

void LineMarkers::RemoveLine(Sci::Line line) {
	// Retain the markers from the deleted line by oring them into the previous line
	if (markers.Length()) {
		if (line > 0) {
			MergeMarkers(line - 1);
		}
		markers.Delete(line);
	}
}

Sci::Line LineMarkers::LineFromHandle(int markerHandle) const noexcept {
	for (Sci::Line line = 0; line < markers.Length(); line++) {
		if (markers[line] && markers[line]->Contains(markerHandle)) {
			return line;
		}
	}
	return -1;
}

int LineMarkers::HandleFromLine(Sci::Line line, int which) const noexcept {
	if (markers.Length() && (line >= 0) && (line < markers.Length()) && markers[line]) {
		MarkerHandleNumber const *pnmh = markers[line]->GetMarkerHandleNumber(which);
		return pnmh ? pnmh->handle : -1;
	}
	return -1;
}

int LineMarkers::NumberFromLine(Sci::Line line, int which) const noexcept {
	if (markers.Length() && (line >= 0) && (line < markers.Length()) && markers[line]) {
		MarkerHandleNumber const *pnmh = markers[line]->GetMarkerHandleNumber(which);
		return pnmh ? pnmh->number : -1;
	}
	return -1;
}

void LineMarkers::MergeMarkers(Sci::Line line) {
	if (markers[line + 1]) {
		if (!markers[line])
			markers[line] = std::make_unique<MarkerHandleSet>();
		markers[line]->CombineWith(markers[line + 1].get());
		markers[line + 1].reset();
	}
}

int LineMarkers::MarkValue(Sci::Line line) const noexcept {
	if (markers.Length() && (line >= 0) && (line < markers.Length()) && markers[line])
		return markers[line]->MarkValue();
	else
		return 0;
}

Sci::Line LineMarkers::MarkerNext(Sci::Line lineStart, int mask) const noexcept {
	if (lineStart < 0)
		lineStart = 0;
	const Sci::Line length = markers.Length();
	for (Sci::Line iLine = lineStart; iLine < length; iLine++) {
		const MarkerHandleSet *onLine = markers[iLine].get();
		if (onLine && ((onLine->MarkValue() & mask) != 0))
			return iLine;
	}
	return -1;
}

int LineMarkers::AddMark(Sci::Line line, int markerNum, Sci::Line lines) {
	handleCurrent++;
	if (!markers.Length()) {
		// No existing markers so allocate one element per line
		markers.InsertEmpty(0, lines);
	}
	if (line >= markers.Length()) {
		return -1;
	}
	if (!markers[line]) {
		// Need new structure to hold marker handle
		markers[line] = std::make_unique<MarkerHandleSet>();
	}
	markers[line]->InsertHandle(handleCurrent, markerNum);

	return handleCurrent;
}

bool LineMarkers::DeleteMark(Sci::Line line, int markerNum, bool all) {
	bool someChanges = false;
	if (markers.Length() && (line >= 0) && (line < markers.Length()) && markers[line]) {
		if (markerNum == -1) {
			someChanges = true;
			markers[line].reset();
		} else {
			someChanges = markers[line]->RemoveNumber(markerNum, all);
			if (markers[line]->Empty()) {
				markers[line].reset();
			}
		}
	}
	return someChanges;
}

void LineMarkers::DeleteMarkFromHandle(int markerHandle) {
	const Sci::Line line = LineFromHandle(markerHandle);
	if (line >= 0) {
		markers[line]->RemoveHandle(markerHandle);
		if (markers[line]->Empty()) {
			markers[line].reset();
		}
	}
}

void LineLevels::Init() {
	levels.DeleteAll();
}

void LineLevels::InsertLine(Sci::Line line) {
	if (levels.Length()) {
		const int level = (line < levels.Length()) ? levels[line] : static_cast<int>(Scintilla::FoldLevel::Base);
		levels.Insert(line, level);
	}
}

void LineLevels::InsertLines(Sci::Line line, Sci::Line lines) {
	if (levels.Length()) {
		const int level = (line < levels.Length()) ? levels[line] : static_cast<int>(Scintilla::FoldLevel::Base);
		levels.InsertValue(line, lines, level);
	}
}

void LineLevels::RemoveLine(Sci::Line line) {
	if (levels.Length()) {
		// Move up following lines but merge header flag from this line
		// to line before to avoid a temporary disappearance causing expansion.
		int firstHeader = levels[line] & static_cast<int>(Scintilla::FoldLevel::HeaderFlag);
		levels.Delete(line);
		if (line == levels.Length()-1) // Last line loses the header flag
			levels[line-1] &= ~static_cast<int>(Scintilla::FoldLevel::HeaderFlag);
		else if (line > 0)
			levels[line-1] |= firstHeader;
	}
}

void LineLevels::ExpandLevels(Sci::Line sizeNew) {
	levels.InsertValue(levels.Length(), sizeNew - levels.Length(), static_cast<int>(Scintilla::FoldLevel::Base));
}

void LineLevels::ClearLevels() {
	levels.DeleteAll();
}

int LineLevels::SetLevel(Sci::Line line, int level, Sci::Line lines) {
	int prev = level;
	if ((line >= 0) && (line < lines)) {
		if (!levels.Length()) {
			ExpandLevels(lines + 1);
		}
		prev = levels[line];
		levels[line] = level;
	}
	return prev;
}

int LineLevels::GetLevel(Sci::Line line) const noexcept {
	if ((line >= 0) && (line < levels.Length())) {
		return levels[line];
	}
	return static_cast<int>(Scintilla::FoldLevel::Base);
}

Scintilla::FoldLevel LineLevels::GetFoldLevel(Sci::Line line) const noexcept {
	if ((line >= 0) && (line < levels.Length())) {
		return static_cast<FoldLevel>(levels[line]);
	}
	return Scintilla::FoldLevel::Base;
}

Sci::Line LineLevels::GetFoldParent(Sci::Line line) const noexcept {
	const FoldLevel level = LevelNumberPart(GetFoldLevel(line));
	for (Sci::Line lineLook = line - 1; lineLook >= 0; lineLook--) {
		const FoldLevel levelTry = GetFoldLevel(lineLook);
		if (LevelIsHeader(levelTry) && LevelNumberPart(levelTry) < level) {
			return lineLook;
		}
	}
	return -1;
}

void LineState::Init() {
	lineStates.DeleteAll();
}

void LineState::InsertLine(Sci::Line line) {
	if (lineStates.Length()) {
		lineStates.EnsureLength(line);
		const int val = (line < lineStates.Length()) ? lineStates[line] : 0;
		lineStates.Insert(line, val);
	}
}

void LineState::InsertLines(Sci::Line line, Sci::Line lines) {
	if (lineStates.Length()) {
		lineStates.EnsureLength(line);
		const int val = (line < lineStates.Length()) ? lineStates[line] : 0;
		lineStates.InsertValue(line, lines, val);
	}
}

void LineState::RemoveLine(Sci::Line line) {
	if (lineStates.Length() > line) {
		lineStates.Delete(line);
	}
}

int LineState::SetLineState(Sci::Line line, int state, Sci::Line lines) {
	int stateOld = state;
	if ((line >= 0) && (line < lines)) {
		lineStates.EnsureLength(lines + 1);
		stateOld = lineStates[line];
		lineStates[line] = state;
	}
	return stateOld;
}

int LineState::GetLineState(Sci::Line line) {
	if (line < 0)
		return 0;
	lineStates.EnsureLength(line + 1);
	return lineStates[line];
}

Sci::Line LineState::GetMaxLineState() const noexcept {
	return lineStates.Length();
}

// Each allocated LineAnnotation is a char array which starts with an AnnotationHeader
// and then has text and optional styles.

struct AnnotationHeader {
	short style;	// Style IndividualStyles implies array of styles
	short lines;
	int length;
};

namespace {

constexpr int IndividualStyles = 0x100;

size_t NumberLines(std::string_view sv) {
	return std::count(sv.begin(), sv.end(), '\n') + 1;
}

std::unique_ptr<char[]>AllocateAnnotation(size_t length, int style) {
	const size_t len = sizeof(AnnotationHeader) + length + ((style == IndividualStyles) ? length : 0);
	return std::make_unique<char[]>(len);
}

}

bool LineAnnotation::Empty() const noexcept {
	return annotations.Length() == 0;
}

void LineAnnotation::Init() {
	ClearAll();
}

void LineAnnotation::InsertLine(Sci::Line line) {
	if (annotations.Length()) {
		annotations.EnsureLength(line);
		annotations.Insert(line, std::unique_ptr<char []>());
	}
}

void LineAnnotation::InsertLines(Sci::Line line, Sci::Line lines) {
	if (annotations.Length()) {
		annotations.EnsureLength(line);
		annotations.InsertEmpty(line, lines);
	}
}

void LineAnnotation::RemoveLine(Sci::Line line) {
	if (annotations.Length() && (line > 0) && (line <= annotations.Length())) {
		annotations[line-1].reset();
		annotations.Delete(line-1);
	}
}

bool LineAnnotation::MultipleStyles(Sci::Line line) const noexcept {
	if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line])
		return reinterpret_cast<AnnotationHeader *>(annotations[line].get())->style == IndividualStyles;
	else
		return false;
}

int LineAnnotation::Style(Sci::Line line) const noexcept {
	if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line])
		return reinterpret_cast<AnnotationHeader *>(annotations[line].get())->style;
	else
		return 0;
}

const char *LineAnnotation::Text(Sci::Line line) const noexcept {
	if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line])
		return annotations[line].get()+sizeof(AnnotationHeader);
	else
		return nullptr;
}

const unsigned char *LineAnnotation::Styles(Sci::Line line) const noexcept {
	if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line] && MultipleStyles(line))
		return reinterpret_cast<unsigned char *>(annotations[line].get() + sizeof(AnnotationHeader) + Length(line));
	else
		return nullptr;
}

void LineAnnotation::SetText(Sci::Line line, const char *text) {
	if (text && (line >= 0)) {
		annotations.EnsureLength(line+1);
		const int style = Style(line);
		annotations[line] = AllocateAnnotation(strlen(text), style);
		char *pa = annotations[line].get();
		assert(pa);
		AnnotationHeader *pah = reinterpret_cast<AnnotationHeader *>(pa);
		pah->style = static_cast<short>(style);
		pah->length = static_cast<int>(strlen(text));
		pah->lines = static_cast<short>(NumberLines(text));
		memcpy(pa+sizeof(AnnotationHeader), text, pah->length);
	} else {
		if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line]) {
			annotations[line].reset();
		}
	}
}

void LineAnnotation::ClearAll() {
	annotations.DeleteAll();
}

void LineAnnotation::SetStyle(Sci::Line line, int style) {
	annotations.EnsureLength(line+1);
	if (!annotations[line]) {
		annotations[line] = AllocateAnnotation(0, style);
	}
	reinterpret_cast<AnnotationHeader *>(annotations[line].get())->style = static_cast<short>(style);
}

void LineAnnotation::SetStyles(Sci::Line line, const unsigned char *styles) {
	if (line >= 0) {
		annotations.EnsureLength(line+1);
		if (!annotations[line]) {
			annotations[line] = AllocateAnnotation(0, IndividualStyles);
		} else {
			const AnnotationHeader *pahSource = reinterpret_cast<AnnotationHeader *>(annotations[line].get());
			if (pahSource->style != IndividualStyles) {
				std::unique_ptr<char[]>allocation = AllocateAnnotation(pahSource->length, IndividualStyles);
				AnnotationHeader *pahAlloc = reinterpret_cast<AnnotationHeader *>(allocation.get());
				pahAlloc->length = pahSource->length;
				pahAlloc->lines = pahSource->lines;
				memcpy(allocation.get() + sizeof(AnnotationHeader), annotations[line].get() + sizeof(AnnotationHeader), pahSource->length);
				annotations[line] = std::move(allocation);
			}
		}
		AnnotationHeader *pah = reinterpret_cast<AnnotationHeader *>(annotations[line].get());
		pah->style = IndividualStyles;
		memcpy(annotations[line].get() + sizeof(AnnotationHeader) + pah->length, styles, pah->length);
	}
}

int LineAnnotation::Length(Sci::Line line) const noexcept {
	if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line])
		return reinterpret_cast<AnnotationHeader *>(annotations[line].get())->length;
	else
		return 0;
}

int LineAnnotation::Lines(Sci::Line line) const noexcept {
	if (annotations.Length() && (line >= 0) && (line < annotations.Length()) && annotations[line])
		return reinterpret_cast<AnnotationHeader *>(annotations[line].get())->lines;
	else
		return 0;
}

void LineTabstops::Init() {
	tabstops.DeleteAll();
}

void LineTabstops::InsertLine(Sci::Line line) {
	if (tabstops.Length()) {
		tabstops.EnsureLength(line);
		tabstops.Insert(line, nullptr);
	}
}

void LineTabstops::InsertLines(Sci::Line line, Sci::Line lines) {
	if (tabstops.Length()) {
		tabstops.EnsureLength(line);
		tabstops.InsertEmpty(line, lines);
	}
}

void LineTabstops::RemoveLine(Sci::Line line) {
	if (tabstops.Length() > line) {
		tabstops[line].reset();
		tabstops.Delete(line);
	}
}

bool LineTabstops::ClearTabstops(Sci::Line line) noexcept {
	if (line < tabstops.Length()) {
		TabstopList *tl = tabstops[line].get();
		if (tl) {
			tl->clear();
			return true;
		}
	}
	return false;
}

bool LineTabstops::AddTabstop(Sci::Line line, int x) {
	tabstops.EnsureLength(line + 1);
	if (!tabstops[line]) {
		tabstops[line] = std::make_unique<TabstopList>();
	}

	TabstopList *tl = tabstops[line].get();
	if (tl) {
		// tabstop positions are kept in order - insert in the right place
		std::vector<int>::iterator it = std::lower_bound(tl->begin(), tl->end(), x);
		// don't insert duplicates
		if (it == tl->end() || *it != x) {
			tl->insert(it, x);
			return true;
		}
	}
	return false;
}

int LineTabstops::GetNextTabstop(Sci::Line line, int x) const noexcept {
	if (line < tabstops.Length()) {
		const TabstopList *tl = tabstops[line].get();
		if (tl) {
			for (const int i : *tl) {
				if (i > x) {
					return i;
				}
			}
		}
	}
	return 0;
}

LineInlayHints::LineInlayHints() : handleCurrent(0) {
}

LineInlayHints::~LineInlayHints() {
}

void LineInlayHints::Init() {
	hints.DeleteAll();
}

void LineInlayHints::InsertLine(Sci::Line line) {
	if (hints.Length()) {
		hints.EnsureLength(line);
		hints.Insert(line, nullptr);
	}
}

void LineInlayHints::InsertLines(Sci::Line line, Sci::Line lines) {
	if (hints.Length()) {
		hints.EnsureLength(line);
		hints.InsertEmpty(line, lines);
	}
}

void LineInlayHints::RemoveLine(Sci::Line line) {
	if (hints.Length() > line) {
		hints[line].reset();
		hints.Delete(line);
	}
}

int LineInlayHints::SetHint(Sci::Line line, Sci::Position position,
	const char* text, int style, bool paddingLeft, bool paddingRight, int handle) {
	if (handle == 0) {
		// Create new hint
		hints.EnsureLength(line + 1);
		if (!hints[line]) {
			hints[line] = std::make_unique<std::vector<InlayHint>>();
		}

		std::vector<InlayHint>* lineHints = hints[line].get();

		// Allocate new handle
		const int hintHandle = ++handleCurrent;

		// Create new hint with handle
		InlayHint hint(hintHandle, position, text ? text : "", style);
		hint.paddingLeft = paddingLeft;
		hint.paddingRight = paddingRight;

		// Insert in sorted order (binary search insertion point)
		auto it = std::lower_bound(lineHints->begin(), lineHints->end(), hint);
		lineHints->insert(it, hint);

		return hintHandle;
	}

	// Update existing hint - search all lines for the hint with this handle
	for (Sci::Line searchLine = 0; searchLine < hints.Length(); searchLine++) {
		std::vector<InlayHint>* lineHints = hints[searchLine].get();
		if (!lineHints) continue;

		auto it = std::find_if(lineHints->begin(), lineHints->end(),
			[handle](const InlayHint& h) { return h.handle == handle; });

		if (it != lineHints->end()) {
			it->text = text ? text : "";
			it->style = style;
			it->paddingLeft = paddingLeft;
			it->paddingRight = paddingRight;
			// Note: line and position are not updated for existing hints
			return handle;
		}
	}

	// Handle not found
	return -1;
}

bool LineInlayHints::GetHint(int hintHandle, Sci::Line &line, Sci::Position &position,
	int &style, const char *&text, bool &paddingLeft, bool &paddingRight) const noexcept {
	// Search all lines for the hint with this handle
	for (Sci::Line searchLine = 0; searchLine < hints.Length(); searchLine++) {
		const std::vector<InlayHint>* lineHints = hints[searchLine].get();
		if (!lineHints) continue;

		auto it = std::find_if(lineHints->begin(), lineHints->end(),
			[hintHandle](const InlayHint& h) { return h.handle == hintHandle; });

		if (it != lineHints->end()) {
			line = searchLine;
			position = it->position;
			style = it->style;
			text = it->text.c_str();
			paddingLeft = it->paddingLeft;
			paddingRight = it->paddingRight;
			return true;
		}
	}
	return false;
}

void LineInlayHints::RemoveHint(int hintHandle) {
	// Search all lines for the hint with this handle
	for (Sci::Line line = 0; line < hints.Length(); line++) {
		std::vector<InlayHint>* lineHints = hints[line].get();
		if (!lineHints) continue;

		auto it = std::find_if(lineHints->begin(), lineHints->end(),
			[hintHandle](const InlayHint& h) { return h.handle == hintHandle; });

		if (it != lineHints->end()) {
			lineHints->erase(it);

			// Clean up empty vector
			if (lineHints->empty()) {
				hints[line].reset();
			}
			return;
		}
	}
}

void LineInlayHints::RemoveHintsInRange(Sci::Line line, Sci::Position start, Sci::Position end) {
	if (line < 0 || line >= hints.Length()) return;

	std::vector<InlayHint>* lineHints = hints[line].get();
	if (!lineHints) return;

	// Remove all hints within the range [start, end)
	auto it = lineHints->begin();
	while (it != lineHints->end()) {
		if (it->position >= start && it->position < end) {
			it = lineHints->erase(it);
		}
		else {
			++it;
		}
	}

	// Clean up empty vector
	if (lineHints->empty()) {
		hints[line].reset();
	}
}

void LineInlayHints::ClearLine(Sci::Line line) {
	if (line >= 0 && line < hints.Length()) {
		hints[line].reset();
	}
}

void LineInlayHints::ClearAll() {
	hints.DeleteAll();
}

const std::vector<InlayHint>* LineInlayHints::GetHints(Sci::Line line) const noexcept {
	if (line >= 0 && line < hints.Length()) {
		return hints.ValueAt(line).get();
	}
	return nullptr;
}

Sci::Position LineInlayHints::GetInlayInfo(void *buffer, Sci::Position bufferSize) const {
	const size_t structSize = sizeof(Scintilla::InlayInfo);
	size_t totalSize = 0;

	// Calculate total size needed
	for (Sci::Line line = 0; line < hints.Length(); line++) {
		if (hints[line]) {
			totalSize += hints[line]->size() * structSize;
		}
	}

	// If buffer is NULL, return required size
	if (!buffer) {
		return static_cast<Sci::Position>(totalSize);
	}

	// Write to buffer
	Scintilla::InlayInfo *dest = static_cast<Scintilla::InlayInfo *>(buffer);
	size_t written = 0;

	for (Sci::Line line = 0; line < hints.Length(); line++) {
		if (hints[line]) {
			for (const InlayHint &hint : *hints[line]) {
				if (written + structSize > static_cast<size_t>(bufferSize)) {
					return static_cast<Sci::Position>(written);
				}
				dest->handle = hint.handle;
				dest->line = line;
				dest->position = hint.position;
				dest->style = hint.style;
				dest->text = hint.text.c_str();
				dest->paddingLeft = hint.paddingLeft;
				dest->paddingRight = hint.paddingRight;
				dest++;
				written += structSize;
			}
		}
	}

	return static_cast<Sci::Position>(written);
}

bool LineInlayHints::HasHints(Sci::Line line) const noexcept {
	const std::vector<InlayHint>* lineHints = GetHints(line);
	return lineHints && !lineHints->empty();
}

void LineInlayHints::AdjustHints(Sci::Line line, Sci::Position position,
	Sci::Position delta) {
	if (line < 0 || line >= hints.Length()) return;

	std::vector<InlayHint>* lineHints = hints[line].get();
	if (!lineHints) return;

	auto it = lineHints->begin();
	while (it != lineHints->end()) {
		if (it->position > position) {
			// Hint is after change - adjust position
			it->position += delta;

			// If delta is negative and hint moved before position, remove it
			if (delta < 0 && it->position < position) {
				it = lineHints->erase(it);
				continue;
			}
		}
		else if (it->position == position && delta < 0) {
			// Deletion at hint position - remove hint
			it = lineHints->erase(it);
			continue;
		}
		++it;
	}

	// Clean up if empty
	if (lineHints->empty()) {
		hints[line].reset();
	}
}

void LineInlayHints::MoveHintsAfterInsert(Sci::Line line, Sci::Position position,
	Sci::Line linesAdded, Sci::Position lastSegmentLength) {
	if (linesAdded <= 0 || line < 0 || line >= hints.Length()) {
		return;
	}
	std::vector<InlayHint>* lineHints = hints[line].get();
	if (!lineHints || lineHints->empty()) {
		return;
	}

	std::vector<InlayHint> moved;
	auto it = lineHints->begin();
	while (it != lineHints->end()) {
		if (it->position > position) {
			moved.push_back(*it);
			it = lineHints->erase(it);
		}
		else {
			++it;
		}
	}

	if (lineHints->empty()) {
		hints[line].reset();
	}
	if (moved.empty()) {
		return;
	}

	const Sci::Line lineTarget = line + linesAdded;
	hints.EnsureLength(lineTarget + 1);
	if (!hints[lineTarget]) {
		hints[lineTarget] = std::make_unique<std::vector<InlayHint>>();
	}
	std::vector<InlayHint>* targetHints = hints[lineTarget].get();

	for (InlayHint& hint : moved) {
		hint.position = lastSegmentLength + (hint.position - position);
		auto insertIt = std::lower_bound(targetHints->begin(), targetHints->end(), hint);
		targetHints->insert(insertIt, hint);
	}
}

void LineInlayHints::MergeLines(Sci::Line lineStart, Sci::Position positionStart,
	Sci::Line lineEnd, Sci::Position positionEnd) {
	if (lineStart < 0 || lineEnd <= lineStart) {
		return;
	}
	if (lineStart >= hints.Length() || lineEnd >= hints.Length()) {
		return;
	}

	std::vector<InlayHint>* startHints = hints[lineStart].get();
	if (startHints) {
		startHints->erase(std::remove_if(startHints->begin(), startHints->end(),
			[positionStart](const InlayHint& hint) {
				return hint.position > positionStart;
			}), startHints->end());
		if (startHints->empty()) {
			hints[lineStart].reset();
			startHints = nullptr;
		}
	}

	std::vector<InlayHint>* endHints = hints[lineEnd].get();
	if (!endHints || endHints->empty()) {
		return;
	}

	std::vector<InlayHint> moved;
	for (const InlayHint& hint : *endHints) {
		if (hint.position >= positionEnd) {
			InlayHint movedHint = hint;
			movedHint.position = positionStart + (hint.position - positionEnd);
			moved.push_back(std::move(movedHint));
		}
	}
	endHints->clear();
	hints[lineEnd].reset();

	if (moved.empty()) {
		return;
	}
	if (!startHints) {
		hints[lineStart] = std::make_unique<std::vector<InlayHint>>();
		startHints = hints[lineStart].get();
	}
	for (InlayHint& hint : moved) {
		auto insertIt = std::lower_bound(startHints->begin(), startHints->end(), hint);
		startHints->insert(insertIt, hint);
	}
}

