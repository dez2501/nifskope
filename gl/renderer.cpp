/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools projectmay not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "renderer.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <GL/glext.h>

#include "gltex.h"
#include "glmesh.h"
#include "glscene.h"
#include "glproperty.h"

#define GLERR( X ) \
{ \
	GLenum err; \
	while ( ( err = glGetError() ) != GL_NO_ERROR ) \
		qWarning() << "GL ERROR (" << X << "): " << (const char *) gluErrorString( err ); \
}

// GL_ARB_shader_objects
PFNGLCREATEPROGRAMOBJECTARBPROC  _glCreateProgramObjectARB  = NULL;
PFNGLDELETEOBJECTARBPROC         _glDeleteObjectARB         = NULL;
PFNGLUSEPROGRAMOBJECTARBPROC     _glUseProgramObjectARB     = NULL;
PFNGLCREATESHADEROBJECTARBPROC   _glCreateShaderObjectARB   = NULL;
PFNGLSHADERSOURCEARBPROC         _glShaderSourceARB         = NULL;
PFNGLCOMPILESHADERARBPROC        _glCompileShaderARB        = NULL;
PFNGLGETOBJECTPARAMETERIVARBPROC _glGetObjectParameterivARB = NULL;
PFNGLATTACHOBJECTARBPROC         _glAttachObjectARB         = NULL;
PFNGLGETINFOLOGARBPROC           _glGetInfoLogARB           = NULL;
PFNGLLINKPROGRAMARBPROC          _glLinkProgramARB          = NULL;
PFNGLGETUNIFORMLOCATIONARBPROC   _glGetUniformLocationARB   = NULL;
PFNGLUNIFORM4FARBPROC            _glUniform4fARB            = NULL;
PFNGLUNIFORM1IARBPROC            _glUniform1iARB            = NULL;

bool shader_initialized = false;
bool shader_ready = false;

void Renderer::initialize( const QGLContext * cx )
{
	if ( shader_initialized )
		return;
	
	shader_initialized = true;
	
	QString extensions( (const char *) glGetString(GL_EXTENSIONS) );
	
	if ( ! extensions.contains( "GL_ARB_shading_language_100" ) || ! extensions.contains( "GL_ARB_shader_objects" )
		|| ! extensions.contains( "GL_ARB_vertex_shader" ) || ! extensions.contains( "GL_ARB_fragment_shader" ) )
	{
		qWarning() << "OpenGL shader extensions not supported";
		return;
	}

	_glCreateProgramObjectARB  = (PFNGLCREATEPROGRAMOBJECTARBPROC) cx->getProcAddress("glCreateProgramObjectARB");
	_glDeleteObjectARB         = (PFNGLDELETEOBJECTARBPROC) cx->getProcAddress("glDeleteObjectARB");
	_glUseProgramObjectARB     = (PFNGLUSEPROGRAMOBJECTARBPROC) cx->getProcAddress("glUseProgramObjectARB");
	_glCreateShaderObjectARB   = (PFNGLCREATESHADEROBJECTARBPROC) cx->getProcAddress("glCreateShaderObjectARB");
	_glShaderSourceARB         = (PFNGLSHADERSOURCEARBPROC) cx->getProcAddress("glShaderSourceARB");
	_glCompileShaderARB        = (PFNGLCOMPILESHADERARBPROC) cx->getProcAddress("glCompileShaderARB");
	_glGetObjectParameterivARB = (PFNGLGETOBJECTPARAMETERIVARBPROC) cx->getProcAddress("glGetObjectParameterivARB");
	_glAttachObjectARB         = (PFNGLATTACHOBJECTARBPROC) cx->getProcAddress("glAttachObjectARB");
	_glGetInfoLogARB           = (PFNGLGETINFOLOGARBPROC) cx->getProcAddress("glGetInfoLogARB");
	_glLinkProgramARB          = (PFNGLLINKPROGRAMARBPROC) cx->getProcAddress("glLinkProgramARB");
	_glGetUniformLocationARB   = (PFNGLGETUNIFORMLOCATIONARBPROC) cx->getProcAddress("glGetUniformLocationARB");
	_glUniform4fARB            = (PFNGLUNIFORM4FARBPROC) cx->getProcAddress("glUniform4fARB");
	_glUniform1iARB            = (PFNGLUNIFORM1IARBPROC) cx->getProcAddress("glUniform1iARB");

	if( !_glCreateProgramObjectARB || !_glDeleteObjectARB || !_glUseProgramObjectARB ||
		!_glCreateShaderObjectARB || !_glShaderSourceARB || !_glCompileShaderARB || 
		!_glGetObjectParameterivARB || !_glAttachObjectARB || !_glGetInfoLogARB || 
		!_glLinkProgramARB || !_glGetUniformLocationARB || !_glUniform4fARB ||
		!_glUniform1iARB )
	{
		qWarning() << "One or more GL_ARB_shader_objects functions were not found";
		return;
	}

	shader_ready = true;
}

QHash<Renderer::ConditionSingle::Type, QString> Renderer::ConditionSingle::compStrs;

Renderer::ConditionSingle::ConditionSingle( const QString & line, bool neg ) : invert( neg )
{
	if ( compStrs.isEmpty() )
	{
		compStrs.insert( EQ, " == " );
		compStrs.insert( NE, " != " );
		compStrs.insert( LE, " <= " );
		compStrs.insert( GE, " >= " );
		compStrs.insert( LT, " < " );
		compStrs.insert( GT, " > " );
		compStrs.insert( AND, " & " );
	}
	
	QHashIterator<Type, QString> i( compStrs );
	int pos = -1;
	while ( i.hasNext() )
	{
		i.next();
		pos = line.indexOf( i.value() );
		if ( pos > 0 )
			break;
	}
	
	if ( pos > 0 )
	{
		left = line.left( pos ).trimmed();
		right = line.right( line.length() - pos - i.value().length() ).trimmed();
		if ( right.startsWith( "\"" ) && right.endsWith( "\"" ) )
			right = right.mid( 1, right.length() - 2 );
		comp = i.key();
	}
	else
	{
		left = line;
		comp = None;
	}
}

QModelIndex Renderer::ConditionSingle::getIndex( const NifModel * nif, const QList<QModelIndex> & iBlocks, QString blkid ) const
{
	QString childid;
	int pos = blkid.indexOf( "/" );
	if ( pos > 0 )
	{
		childid = blkid.right( blkid.length() - pos - 1 );
		blkid = blkid.left( pos );
	}
	foreach ( QModelIndex iBlock, iBlocks )
	{
		if ( nif->inherits( iBlock, blkid ) )
		{
			if ( childid.isEmpty() )
				return iBlock;
			else
				return nif->getIndex( iBlock, childid );
		}
	}
	return QModelIndex();
}

bool Renderer::ConditionSingle::eval( const NifModel * nif, const QList<QModelIndex> & iBlocks ) const
{
	QModelIndex iLeft = getIndex( nif, iBlocks, left );
	if ( ! iLeft.isValid() )
		return invert;
	if ( comp == None )
		return ! invert;
	NifValue val = nif->getValue( iLeft );
	if ( val.isString() )
		return compare( val.toString(), right ) ^ invert;
	else if ( val.isCount() )
		return compare( val.toCount(), right.toUInt() ) ^ invert;
	else if ( val.isFloat() )
		return compare( val.toFloat(), (float) right.toDouble() ) ^ invert;
	else
		return false;
}

bool Renderer::ConditionGroup::eval( const NifModel * nif, const QList<QModelIndex> & iBlocks ) const
{
	if ( conditions.isEmpty() )
		return true;
	
	if ( isOrGroup() )
	{
		foreach ( Condition * cond, conditions )
			if ( cond->eval( nif, iBlocks ) )
				return true;
		return false;
	}
	else
	{
		foreach ( Condition * cond, conditions )
			if ( ! cond->eval( nif, iBlocks ) )
				return false;
		return true;
	}
}

void Renderer::ConditionGroup::addCondition( Condition * c )
{
	conditions.append( c );
}

Renderer::Shader::Shader( const QString & n, GLenum t ) : name( n ), id( 0 ), status( false ), type( t )
{
	id = _glCreateShaderObjectARB( type );
}

Renderer::Shader::~Shader()
{
	if ( id )
		_glDeleteObjectARB( id );
}

bool Renderer::Shader::load( const QString & filepath )
{
	try
	{
		QFile file( filepath );
		if ( ! file.open( QIODevice::ReadOnly ) )
			throw QString( "couldn't open %1 for read access" ).arg( filepath );
		
		QByteArray data = file.readAll();
		
		const char * src = data.constData();
		
		_glShaderSourceARB( id, 1, & src, 0 );
		_glCompileShaderARB( id );
		
		GLint result;
		_glGetObjectParameterivARB( id, GL_OBJECT_COMPILE_STATUS_ARB, & result );
		
		if ( result != GL_TRUE )
		{
			int logLen;
			_glGetObjectParameterivARB( id, GL_OBJECT_INFO_LOG_LENGTH_ARB, & logLen );
			char * log = new char[ logLen ];
			_glGetInfoLogARB( id, logLen, 0, log );
			QString errlog( log );
			delete[] log;
			throw errlog;
		}
	}
	catch ( QString err )
	{
		status = false;
		qWarning() << "error loading shader" << name << ":\r\n" << err.toAscii().data();
		return false;
	}
	status = true;
	return true;
}

Renderer::Program::Program( const QString & n ) : name( n ), id( 0 ), status( false )
{
	id = _glCreateProgramObjectARB();
}

Renderer::Program::~Program()
{
	if ( id )
		_glDeleteObjectARB( id );
}

bool Renderer::Program::load( const QString & filepath, Renderer * renderer )
{
	try
	{
		QFile file( filepath );
		if ( ! file.open( QIODevice::ReadOnly ) )
			throw QString( "couldn't open %1 for read access" ).arg( filepath );
		
		QTextStream stream( &file );
		
		QStack<ConditionGroup *> chkgrps;
		chkgrps.push( & conditions );
		
		while ( ! stream.atEnd() )
		{
			QString line = stream.readLine().trimmed();
			
			if ( line.startsWith( "shaders" ) )
			{
				QStringList list = line.simplified().split( " " );
				for ( int i = 1; i < list.count(); i++ )
				{
					Shader * shader = renderer->shaders.value( list[ i ] );
					if ( shader )
					{
						if ( shader->status )
							_glAttachObjectARB( id, shader->id );
						else
							throw QString( "depends on shader %1 which was not compiled successful" ).arg( list[ i ] );
					}
					else
						throw QString( "shader %1 not found" ).arg( list[ i ] );
				}
			}
			else if ( line.startsWith( "checkgroup" ) )
			{
				QStringList list = line.simplified().split( " " );
				if ( list.value( 1 ) == "begin" )
				{
					ConditionGroup * group = new ConditionGroup( list.value( 2 ) == "or" );
					chkgrps.top()->addCondition( group );
					chkgrps.push( group );
				}
				else if ( list.value( 1 ) == "end" )
				{
					if ( chkgrps.count() > 1 )
						chkgrps.pop();
					else
						throw QString( "mismatching checkgroup end tag" );
				}
				else
					throw QString( "expected begin or end after checkgroup" );
			}
			else if ( line.startsWith( "check" ) )
			{
				line = line.remove( 0, 5 ).trimmed();
				
				bool invert = false;
				if ( line.startsWith ( "not " ) )
				{
					invert = true;
					line = line.remove( 0, 4 ).trimmed();
				}
				
				chkgrps.top()->addCondition( new ConditionSingle( line, invert ) );
			}
			else if ( line.startsWith( "texcoords" ) )
			{
				line = line.remove( 0, 9 ).simplified();
				QStringList list = line.split( " " );
				bool ok;
				int unit = list.value( 0 ).toInt( & ok );
				QString id = list.value( 1 ).toLower();
				if ( ! ok || id.isEmpty() )
					throw QString( "malformed texcoord tag" );
				if ( id != "tangents" && TexturingProperty::getId( id ) < 0 )
					throw QString( "texcoord tag referres to unknown texture id '%1'" ).arg( id );
				if ( texcoords.contains( unit ) )
					throw QString( "texture unit %1 is assigned twiced" ).arg( unit );
				texcoords.insert( unit, id );
			}
		}
		
		_glLinkProgramARB( id );
		
		int result;
		_glGetObjectParameterivARB( id, GL_OBJECT_LINK_STATUS_ARB, & result );
		
		if ( result != GL_TRUE )
		{
			int logLen;
			_glGetObjectParameterivARB( id, GL_OBJECT_INFO_LOG_LENGTH_ARB, & logLen );
			char * log = new char[ logLen ];
			_glGetInfoLogARB( id, logLen, 0, log );
			QString errlog( log );
			delete[] log;
			throw errlog;
		}
	}
	catch ( QString x )
	{
		status = false;
		qWarning() << "error loading shader program " << name << ":\r\n" << x.toAscii().data();
		return false;
	}
	status = true;
	return true;
}

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
	releaseShaders();
}

void Renderer::updateShaders()
{
	releaseShaders();
	
	QDir dir( QApplication::applicationDirPath() );
	dir.cd( "shaders" );
	
	dir.setNameFilters( QStringList() << "*.vert" );
	foreach ( QString name, dir.entryList() )
	{
		Shader * shader = new Shader( name, GL_VERTEX_SHADER_ARB );
		shader->load( dir.filePath( name ) );
		shaders.insert( name, shader );
	}
	
	dir.setNameFilters( QStringList() << "*.frag" );
	foreach ( QString name, dir.entryList() )
	{
		Shader * shader = new Shader( name, GL_FRAGMENT_SHADER_ARB );
		shader->load( dir.filePath( name ) );
		shaders.insert( name, shader );
	}
	
	dir.setNameFilters( QStringList() << "*.prog" );
	foreach ( QString name, dir.entryList() )
	{
		Program * program = new Program( name );
		program->load( dir.filePath( name ), this );
		programs.insert( name, program );
	}
}

void Renderer::releaseShaders()
{
	qDeleteAll( programs );
	programs.clear();
	qDeleteAll( shaders );
	shaders.clear();
}

QString Renderer::setupProgram( Mesh * mesh, const QString & hint )
{
	GLERR( "setup" )
	
	PropertyList props;
	mesh->activeProperties( props );
	
	QList<QModelIndex> iBlocks;
	iBlocks << mesh->index();
	iBlocks << mesh->iData;
	foreach ( Property * p, props.list() )
		iBlocks.append( p->index() );
	
	if ( ! shader_ready || ! mesh->scene->shading )
	{
		setupFixedFunction( mesh, props );
		return QString( "fixed function pipeline" );
	}
	
	if ( ! hint.isEmpty() )
	{
		Program * program = programs.value( hint );
		if ( program && program->status && setupProgram( program, mesh, props, iBlocks ) )
			return hint;
	}
	
	foreach ( Program * program, programs )
	{
		if ( program->status && setupProgram( program, mesh, props, iBlocks ) )
			return program->name;
	}
	
	stopProgram();
	setupFixedFunction( mesh, props );
	return QString( "fixed function pipeline" );
}

void Renderer::stopProgram()
{
	if ( shader_ready )
		_glUseProgramObjectARB( 0 );
	resetTextureUnits();
}

bool Renderer::setupProgram( Program * prog, Mesh * mesh, const PropertyList & props, const QList<QModelIndex> & iBlocks )
{
	const NifModel * nif = qobject_cast<const NifModel *>( mesh->index().model() );
	if ( ! mesh->index().isValid() || ! nif )
		return false;
	
	if ( ! prog->conditions.eval( nif, iBlocks ) )
		return false;
	
	_glUseProgramObjectARB( prog->id );
	
	// texturing
	
	TexturingProperty * texprop = props.get< TexturingProperty >();
	
	GLERR( 0 )
	
	int texunit = 0;
	
	GLint uniBaseMap = _glGetUniformLocationARB( prog->id, "BaseMap" );
	
	if ( uniBaseMap >= 0 )
	{
		if ( ! texprop || ! activateTextureUnit( texunit ) || ! texprop->bind( 0 ) )
			return false;
		
		_glUniform1iARB( uniBaseMap, texunit++ );
	}
	
	GLint uniNormalMap = _glGetUniformLocationARB( prog->id, "NormalMap" );
	
	if ( uniNormalMap >= 0 )
	{
		QString fname = texprop->fileName( 0 );
		if ( fname.isEmpty() )
			return false;
		
		int pos = fname.indexOf( "_" );
		if ( pos >= 0 )
			fname = fname.left( pos ) + "_n.dds";
		else if ( ( pos = fname.lastIndexOf( "." ) ) >= 0 )
			fname = fname.insert( pos, "_n" );
		
		if ( ! activateTextureUnit( texunit ) || ! texprop->bind( 0, fname ) )
			return false;
		
		_glUniform1iARB( uniNormalMap, texunit++ );
	}
	
	QMapIterator<int, QString> itx( prog->texcoords );
	while ( itx.hasNext() )
	{
		itx.next();
		if ( ! activateTextureUnit( itx.key() ) )
			return false;
		if ( itx.value() == "tangents" )
		{
			if ( ! mesh->transTangents.count() )
				return false;
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 3, GL_FLOAT, 0, mesh->transTangents.data() );
		}
		else
		{
			int txid = TexturingProperty::getId( itx.value() );
			if ( txid < 0 )
				return false;
			int set = texprop->coordSet( txid );
			if ( set >= 0 && set < mesh->coords.count() && mesh->coords[set].count() )
			{
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 2, GL_FLOAT, 0, mesh->coords[set].data() );
			}
			else
				return false;
		}
	}
	
	// setup lighting
	
	glEnable( GL_LIGHTING );
	
	// setup blending
	
	glProperty( props.get< AlphaProperty >() );
	
	// setup vertex colors
	
	//glProperty( props.get< VertexColorProperty >(), glIsEnabled( GL_COLOR_ARRAY ) );
	
	// setup material
	
	glProperty( props.get< MaterialProperty >(), props.get< SpecularProperty >() );

	// setup z buffer
	
	glProperty( props.get< ZBufferProperty >() );
	
	// setup stencil
	
	glProperty( props.get< StencilProperty >() );
	
	// wireframe ?
	
	glProperty( props.get< WireframeProperty >() );

	/*
	GLERR( 1 )
	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );

	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_FALSE );
	
	Color4 a( 0.4, 0.4, 0.4, 1.0 );
	Color4 d( 0.8, 0.8, 0.8, 1.0 );
	Color4 s( 1.0, 1.0, 1.0, 1.0 );
	glMaterialf( GL_FRONT_AND_BACK, GL_SHININESS, 33.0 );
	glMaterial( GL_FRONT_AND_BACK, GL_AMBIENT, a );
	glMaterial( GL_FRONT_AND_BACK, GL_DIFFUSE, d );
	glMaterial( GL_FRONT_AND_BACK, GL_SPECULAR, s );

	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	glDisable( GL_COLOR_MATERIAL );
	glColor( Color4( 1.0, 1.0, 1.0, 1.0 ) );

	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	*/
	GLERR( "prog" )
	
	return true;
}

void Renderer::setupFixedFunction( Mesh * mesh, const PropertyList & props )
{
	// setup lighting
	
	glEnable( GL_LIGHTING );
	
	// setup blending
	
	glProperty( props.get< AlphaProperty >() );
	
	// setup vertex colors
	
	glProperty( props.get< VertexColorProperty >(), glIsEnabled( GL_COLOR_ARRAY ) );
	
	// setup material
	
	glProperty( props.get< MaterialProperty >(), props.get< SpecularProperty >() );

	// setup texturing
	
	glProperty( props.get< TexturingProperty >() );
	
	// setup z buffer
	
	glProperty( props.get< ZBufferProperty >() );
	
	// setup stencil
	
	glProperty( props.get< StencilProperty >() );
	
	// wireframe ?
	
	glProperty( props.get< WireframeProperty >() );

	// normalize
	
	if ( glIsEnabled( GL_NORMAL_ARRAY ) )
		glEnable( GL_NORMALIZE );
	else
		glDisable( GL_NORMALIZE );

	// setup multitexturing
	
	TexturingProperty * texprop = props.get< TexturingProperty >();
	if ( texprop )
	{
		int stage = 0;
		
		if ( texprop->bind( 1, mesh->coords, stage ) )
		{	// dark
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA );
			
			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0 );
		}
		
		if ( texprop->bind( 0, mesh->coords, stage ) )
		{	// base
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA );
			
			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0 );
		}
		
		if ( texprop->bind( 2, mesh->coords, stage ) )
		{	// detail
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA );
			
			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2.0 );
		}
		
		if ( texprop->bind( 6, mesh->coords, stage ) )
		{	// decal 0
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
			
			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0 );
		}
		
		if ( texprop->bind( 7, mesh->coords, stage ) )
		{	// decal 1
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
			
			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0 );
		}
		
		if ( texprop->bind( 4, mesh->coords, stage ) )
		{	// glow
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_ADD );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
			
			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
			
			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0 );
		}
	}
	
	GLERR( "fixed" )
}

