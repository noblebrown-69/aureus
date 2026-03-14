# Aureus - Digital Commonplace Book

A modern, hierarchical journaling application built with GTK+3 and SQLite. Organize your thoughts, ideas, and writings in a structured commonplace book format.

## Features

- **Hierarchical Organization**: Create customizable chapters/folders to organize your entries
- **Rich Text Editing**: Full-featured text editor with word wrapping and formatting
- **Drag & Drop**: Easily move entries between chapters
- **Search & Navigation**: Quick access to all your entries with expandable chapter view
- **Persistent Storage**: SQLite database ensures your writings are safely stored
- **Cross-Platform**: Built with GTK+3 for Linux compatibility

## Screenshots

*[Add screenshots here when available]*

## Installation

### Prerequisites

- GTK+ 3.0 development libraries
- SQLite 3 development libraries
- C++17 compatible compiler (GCC recommended)

On Ubuntu/Debian:
```bash
sudo apt-get install libgtk-3-dev libsqlite3-dev build-essential
```

On Fedora/CentOS:
```bash
sudo dnf install gtk3-devel sqlite-devel gcc-c++
```

### Building

```bash
make
```

### Running

```bash
./Aureus
```

## Usage

### Creating Chapters
- Click the "New Chapter" button in the toolbar
- Right-click on existing chapters to rename or delete them

### Writing Entries
- Click "New" to create a new entry
- Use the title field and main text area for your content
- Add tags for better organization
- Click "Save" or use Ctrl+S to save your work

### Organizing Content
- Drag entries between chapters to reorganize
- All changes are automatically saved to the database

## Project Structure

```
aureus/
├── Makefile          # Build configuration
├── tome.cpp          # Main application source
├── json.hpp          # JSON library (header-only)
├── README.md         # This file
├── LICENSE           # MIT License
└── .gitignore       # Git ignore rules
```

## Development

### Dependencies

- GTK+ 3.0
- SQLite 3
- C++17 Standard Library

### Building from Source

```bash
git clone https://github.com/yourusername/aureus.git
cd aureus
make
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built upon the original Vibe Journal codebase
- GTK+ for the excellent GUI toolkit
- SQLite for reliable data storage

## Version History

### v1.0 (2026-03-13)
- Initial release with hierarchical chapter system
- Drag & drop entry organization
- Complete GTK+3 interface
- SQLite database backend