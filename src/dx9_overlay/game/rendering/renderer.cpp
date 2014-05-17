#include <utils/algorithm.h>

#include "renderer.h"
#include "renderbase.h"

#include <boost/range/algorithm.hpp>

renderer::RenderObjects	renderer::_RenderObjects;
boost::mutex			renderer::_mtx;

int renderer::Add(SharedRenderObject Object)
{
	boost::lock_guard<boost::mutex> l(_mtx);

	int id = 0;

	if(!_RenderObjects.empty())
		for(;;id++)
			if(_RenderObjects.find(id) == _RenderObjects.end())
				break;
	
	_RenderObjects[id] = Object;

	return id;
}

bool renderer::Remove(int id)
{
	boost::lock_guard<boost::mutex> l(_mtx);

	if(_RenderObjects.empty())
		return false;

	if(_RenderObjects.find(id) == _RenderObjects.end())
		return false;

	auto ptr = _RenderObjects[id];
	if(ptr->_isMarkedForDeletion)
		return false;

	ptr->Destroy();
	return true;
}

void renderer::Draw(IDirect3DDevice9 *pDevice)
{
	boost::lock_guard<boost::mutex> l(_mtx);

	if(_RenderObjects.empty())
		return;

	erase_if(_RenderObjects, [&](int id, SharedRenderObject obj) -> bool 
	{
		if(obj->_isMarkedForDeletion)
		{
			obj->ReleaseResourcesForDeletion(pDevice);
			return obj->CanBeDeleted();
		}

		return false;
	});


	for(auto it = _RenderObjects.begin(); it != _RenderObjects.end(); it ++)
	{
		if(it->second->_hasToBeInitialised)
		{
			if(!it->second->LoadResource(pDevice))
				continue;

			it->second->_hasToBeInitialised = false;
		}

		if(it->second->_firstDrawAfterReset)
		{
			it->second->FirstDrawAfterReset(pDevice);
			it->second->_firstDrawAfterReset = false;
		}

		if(it->second->_resourceChanged)
		{
			it->second->ReleaseResourcesForDeletion(pDevice);
			if(!it->second->LoadResource(pDevice))
				continue;

			it->second->_resourceChanged = false;
		}

		it->second->Draw(pDevice);
	}
}

void renderer::Reset(IDirect3DDevice9 *pDevice)
{
	boost::lock_guard<boost::mutex> l(_mtx);

	if(_RenderObjects.empty())
		return;
	
	for(auto it = _RenderObjects.begin(); it != _RenderObjects.end(); it ++)
	{
		it->second->Reset(pDevice);
		it->second->_firstDrawAfterReset = true;
	}
}

void renderer::ShowAll()
{
	boost::lock_guard<boost::mutex> l(_mtx);

	if(_RenderObjects.empty())
		return;

	for(auto it = _RenderObjects.begin(); it != _RenderObjects.end();it ++)
	{
		if(it->second->_isMarkedForDeletion)
			continue;

		it->second->Show();
	}
}

void renderer::HideAll()
{
	boost::lock_guard<boost::mutex> l(_mtx);

	if(_RenderObjects.empty())
		return;

	for(auto it = _RenderObjects.begin(); it != _RenderObjects.end();it ++)
	{
		if(it->second->_isMarkedForDeletion)
			continue;

		it->second->Hide();
	}
}

void renderer::DestroyAll()
{
	boost::lock_guard<boost::mutex> l(_mtx);

	if(_RenderObjects.empty())
		return;

	for(auto it = _RenderObjects.begin(); it != _RenderObjects.end();it ++)
	{
		if(it->second->_isMarkedForDeletion)
			continue;

		it->second->Destroy();
	}
}