#include "GMV.h"
#include "../Misc.h"

#define CONERT_MASK 0xFF000000
#define CONVERT_MAGIC 0xA1000000

namespace Gamebuino_Meta {

GMV::GMV() {
	valid = false;
}

GMV::GMV(Image* _img, char* filename) {
	valid = false;
	img = _img;
	img->frames = 1;
	file = SD.open(filename, FILE_WRITE);
	if (!file) {
		// couldn't find file, perhaps SD isn't inited yet, falling back to lazy init...
		img->frames = 0;
		return;
	}
	file.rewind();
	uint16_t header = f_read16(&file);
	if (header == 0x4D42) {
		// we got a BMP image!
		BMP bmp = BMP(&file, img);
		if (!bmp.isValid()) {
			return;
		}
		uint16_t name_len = strlen(filename);
		char _filename[name_len + 1];
		strcpy(_filename, filename);
		
		_filename[name_len - 3] = 'G';
		_filename[name_len - 2] = 'M';
		_filename[name_len - 1] = 'V';
		_filename[name_len] = '\0';
		
		uint32_t creatorBits = bmp.getCreatorBits();
		if ((creatorBits & CONERT_MASK) != CONVERT_MAGIC || !SD.exists(_filename)) {
			// we still need to convert...
			convertFromBMP(bmp, _filename);
		}
		file.close();
		file = SD.open(_filename, FILE_WRITE);
		if (!file) {
			return;
		}
		file.rewind();
		header = f_read16(&file);
	}
	if (header != 0x5647) { // header "GV"
		// invalid header!
		return;
	}
	header_size = f_read16(&file);
	file.seekCur(1); // trash version byte
	img->_width = f_read16(&file);
	img->_height = f_read16(&file);
	img->frames = f_read16(&file);
	file.seekCur(1); // trash indexed checking for now
	img->transparentColor = f_read16(&file);
	file.seekSet(header_size);
	
	if (!img->_buffer) {
		img->allocateBuffer();
	}
	
	valid = true;
}

bool GMV::isValid() {
	return valid;
}

void GMV::convertFromBMP(BMP& bmp, char* newname) {
	img->allocateBuffer();
	if (SD.exists(newname) && !SD.remove(newname)) {
		return;
	}
	File f = SD.open(newname, FILE_WRITE);
	if (!f) {
		return;
	}
	f_write16(0x5647, &f); // header "GV"
	f_write16(14, &f); // header size
	f.write((uint8_t)0); // version
	f_write16(img->_width, &f); // image width
	f_write16(img->_height, &f); // image height
	f_write16(img->frames, &f); // number of frames
	f.write(bmp.depth == 4 ? 1 : 0);
	f_write16(0, &f); // TODO: transaprent color
	for (uint16_t frame = 0; frame < img->frames; frame++) {
		bmp.readFrame(frame, img->frames, img->_height, &file, img->_buffer);
		writeFrame(&f);
	}
	bmp.setCreatorBits(&file, CONVERT_MAGIC);
	f.close();
}

void GMV::writeFrame() {
	writeFrame(&file);
}

void GMV::writeColor(File* f, uint16_t color, uint8_t count) {
	if (count > 1) {
		count |= 0x80;
		f->write(count);
	}
	uint16_t* index = (uint16_t*)img->colorIndex;
	for (uint8_t i = 0; i < 16; i++) {
		if (index[i] == color) {
			f->write(i);
			return;
		}
	}
	f->write(0x80);
	f_write16(color, f);
}

void GMV::writeFrame(File* f) {
	uint16_t pixels = (img->getBufferSize() + 1) / 2; 
	uint16_t i = 1;
	uint16_t* buf = img->_buffer;
	uint16_t color = buf[0];
	uint16_t count = 1;
	for (; i < pixels; i++) {
		if (buf[i] == color && count < 0x7F) {
			count++;
			continue;
		}
		// ok we need to write stuff
		writeColor(f, color, count);
		count = 1;
		color = buf[i];
	}
	writeColor(f, color, count);
}

void GMV::readFrame() {
	uint16_t* buf = img->_buffer;
	uint16_t pixels = (img->getBufferSize() + 1) / 2; 
	uint16_t pixels_current = 0;
	
	uint16_t* index = (uint16_t*)img->colorIndex;
	
	uint8_t count = 0;
	uint8_t i;
	uint16_t color = 0;
	while (pixels_current < pixels) {
		count = file.read();
		if (count == 0x80) {
			// we have a single, un-altered pixel
			file.read(&color, 2);
			buf[pixels_current] = color;
			pixels_current++;
			continue;
		}
		if (!(count & 0x80)) {
			// single indexed color
			buf[pixels_current] = index[count];
			pixels_current++;
			continue;
		}
		// ok we actually have multiple pixels
		count &= 0x7F;
		i = file.read();
		if (i == 0x80) {
			file.read(&color, 2);
		} else {
			color = index[i];
		}
		for (i = 0; i < count; i++) {
			buf[pixels_current] = color;
			pixels_current++;
		}
	}
}

void GMV::setFrame(uint16_t frame) {
	file.seekSet(header_size);
	if (!frame) {
		return;
	}
	for (uint16_t f = 0; f < frame; f++) {
		readFrame(); // unfortunatelly we don't have a better way
	}
}

}
