// Scintilla source code edit control
/** @file PerLine.h
 ** Manages data associated with each line of the document
 **/
// Copyright 1998-2009 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef PERLINE_H
#define PERLINE_H

namespace Scintilla::Internal {

/**
 * This holds the marker identifier and the marker type to display.
 * MarkerHandleNumbers are members of lists.
 */
struct MarkerHandleNumber {
	int handle;
	int number;
	MarkerHandleNumber(int handle_, int number_) noexcept : handle(handle_), number(number_) {}
};

/**
 * A marker handle set contains any number of MarkerHandleNumbers.
 */
class MarkerHandleSet {
	std::forward_list<MarkerHandleNumber> mhList;

public:
	MarkerHandleSet();
	bool Empty() const noexcept;
	int MarkValue() const noexcept;	///< Bit set of marker numbers.
	bool Contains(int handle) const noexcept;
	bool InsertHandle(int handle, int markerNum);
	void RemoveHandle(int handle);
	bool RemoveNumber(int markerNum, bool all);
	void CombineWith(MarkerHandleSet *other) noexcept;
	MarkerHandleNumber const *GetMarkerHandleNumber(int which) const noexcept;
};

class LineMarkers : public PerLine {
	SplitVector<std::unique_ptr<MarkerHandleSet>> markers;
	/// Handles are allocated sequentially and should never have to be reused as 32 bit ints are very big.
	int handleCurrent;
public:
	LineMarkers() : handleCurrent(0) {
	}
	void Init() override;
	void InsertLine(Sci::Line line) override;
	void InsertLines(Sci::Line line, Sci::Line lines) override;
	void RemoveLine(Sci::Line line) override;

	int MarkValue(Sci::Line line) const noexcept;
	Sci::Line MarkerNext(Sci::Line lineStart, int mask) const noexcept;
	int AddMark(Sci::Line line, int markerNum, Sci::Line lines);
	void MergeMarkers(Sci::Line line);
	bool DeleteMark(Sci::Line line, int markerNum, bool all);
	void DeleteMarkFromHandle(int markerHandle);
	Sci::Line LineFromHandle(int markerHandle) const noexcept;
	int HandleFromLine(Sci::Line line, int which) const noexcept;
	int NumberFromLine(Sci::Line line, int which) const noexcept;
};

class LineLevels : public PerLine {
	SplitVector<int> levels;
public:
	LineLevels() {
	}
	void Init() override;
	void InsertLine(Sci::Line line) override;
	void InsertLines(Sci::Line line, Sci::Line lines) override;
	void RemoveLine(Sci::Line line) override;

	void ExpandLevels(Sci::Line sizeNew=-1);
	void ClearLevels();
	int SetLevel(Sci::Line line, int level, Sci::Line lines);
	int GetLevel(Sci::Line line) const noexcept;
	FoldLevel GetFoldLevel(Sci::Line line) const noexcept;
	Sci::Line GetFoldParent(Sci::Line line) const noexcept;
};

class LineState : public PerLine {
	SplitVector<int> lineStates;
public:
	LineState() {
	}
	void Init() override;
	void InsertLine(Sci::Line line) override;
	void InsertLines(Sci::Line line, Sci::Line lines) override;
	void RemoveLine(Sci::Line line) override;

	int SetLineState(Sci::Line line, int state, Sci::Line lines);
	int GetLineState(Sci::Line line);
	Sci::Line GetMaxLineState() const noexcept;
};

class LineAnnotation : public PerLine {
	SplitVector<std::unique_ptr<char []>> annotations;
public:
	LineAnnotation() {
	}

	[[nodiscard]] bool Empty() const noexcept;
	void Init() override;
	void InsertLine(Sci::Line line) override;
	void InsertLines(Sci::Line line, Sci::Line lines) override;
	void RemoveLine(Sci::Line line) override;

	bool MultipleStyles(Sci::Line line) const noexcept;
	int Style(Sci::Line line) const noexcept;
	const char *Text(Sci::Line line) const noexcept;
	const unsigned char *Styles(Sci::Line line) const noexcept;
	void SetText(Sci::Line line, const char *text);
	void ClearAll();
	void SetStyle(Sci::Line line, int style);
	void SetStyles(Sci::Line line, const unsigned char *styles);
	int Length(Sci::Line line) const noexcept;
	int Lines(Sci::Line line) const noexcept;
};

struct InlayHint {
	int handle;                   // Unique handle for this hint
	Sci::Position position;      // Position in line where hint appears (0-based)
	std::string text;             // Hint text to display
	int style;                    // Style number for rendering
	double width;             // Cached width (calculated during layout)
	bool paddingLeft;             // Add visual padding before hint
	bool paddingRight;            // Add visual padding after hint

	InlayHint(int hdl, Sci::Position pos, const char* txt, int sty)
		: handle(hdl), position(pos), text(txt), style(sty), width(0.0),
		paddingLeft(true), paddingRight(true) {
	}

	// For sorting by position
	bool operator<(const InlayHint& other) const {
		return position < other.position;
	}
};

class LineInlayHints : public PerLine {
	// Store hints per-line as sorted vectors (sorted by position)
	SplitVector<std::unique_ptr<std::vector<InlayHint>>> hints;
	/// Handles are allocated sequentially and should never have to be reused as 32 bit ints are very big.
	int handleCurrent;

public:
	LineInlayHints();
	// Deleted copy/move constructors (following Scintilla patterns)
	LineInlayHints(const LineInlayHints&) = delete;
	LineInlayHints(LineInlayHints&&) = delete;
	void operator=(const LineInlayHints&) = delete;
	void operator=(LineInlayHints&&) = delete;
	~LineInlayHints() override;

	// PerLine interface
	void Init() override;
	void InsertLine(Sci::Line line) override;
	void InsertLines(Sci::Line line, Sci::Line lines) override;
	void RemoveLine(Sci::Line line) override;

	// Core operations
	int SetHint(Sci::Line line, Sci::Position position, const char* text, int style, bool paddingLeft, bool paddingRight, int handle);
	bool GetHint(int hintHandle, Sci::Line &line, Sci::Position &position, int &style, const char *&text, bool &paddingLeft, bool &paddingRight) const noexcept;
	void RemoveHint(int hintHandle);
	void RemoveHintsInRange(Sci::Line line, Sci::Position start, Sci::Position end);
	void ClearLine(Sci::Line line);
	void ClearAll();

	// Query operations
	const std::vector<InlayHint>* GetHints(Sci::Line line) const noexcept;
	Sci::Position GetInlayInfo(void *buffer, Sci::Position bufferSize) const;
	bool HasHints(Sci::Line line) const noexcept;

	// Position adjustment (when text inserted/deleted)
	void AdjustHints(Sci::Line line, Sci::Position position, Sci::Position delta);
	void MoveHintsAfterInsert(Sci::Line line, Sci::Position position, Sci::Line linesAdded, Sci::Position lastSegmentLength);
	void MergeLines(Sci::Line lineStart, Sci::Position positionStart, Sci::Line lineEnd, Sci::Position positionEnd);
};

typedef std::vector<int> TabstopList;

class LineTabstops : public PerLine {
	SplitVector<std::unique_ptr<TabstopList>> tabstops;
public:
	LineTabstops() {
	}
	void Init() override;
	void InsertLine(Sci::Line line) override;
	void InsertLines(Sci::Line line, Sci::Line lines) override;
	void RemoveLine(Sci::Line line) override;

	bool ClearTabstops(Sci::Line line) noexcept;
	bool AddTabstop(Sci::Line line, int x);
	int GetNextTabstop(Sci::Line line, int x) const noexcept;
};

}

#endif
