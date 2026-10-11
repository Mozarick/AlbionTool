#include "../Headers/MyImg.h"


MyImg::MyImg(const string& categ, const Vector2f& pos)
	: category(categ)
{
	string path = "./img/" + categ + ".png";
	if (!texture.loadFromFile(path)) {
		cout << "Load image error: " << path << '\n';
	}

	sprite.setTexture(texture);
	sprite.setPosition(pos);
	sprite.setScale(Vector2f(0.25f, 0.25f));
	
}

//MyImg::~MyImg() {
//	cout << "~MyImg\n";
//	delete image;
//	delete texture;
//	delete sprite;
//}


MI_Info::MI_Info(const Vector2f& pos) 
	:pos (pos)
{

}



